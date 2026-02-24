// SntpClient.cpp
#include "SntpClient.h"

#include <QDateTime>
#include <QtEndian>
#include <algorithm>
#include <climits>

namespace
{
  // NTP epoch starts 1900-01-01; Unix epoch starts 1970-01-01.
  // Difference is exactly 70 years (including 17 leap years).
  constexpr quint32 NTP_UNIX_OFFSET_S = 2208988800u;

  // Parse a 64-bit NTP timestamp (big-endian) at byte offset `off` in `data`.
  // Returns Unix time as milliseconds (integer).
  qint64 ntpTimestampToMs (QByteArray const& data, int off)
  {
    if (data.size () < off + 8)
      return 0;

    quint32 secs = qFromBigEndian<quint32> (
      reinterpret_cast<uchar const *> (data.constData () + off));
    quint32 frac = qFromBigEndian<quint32> (
      reinterpret_cast<uchar const *> (data.constData () + off + 4));

    // Convert NTP seconds to Unix seconds.
    qint64 unix_sec = static_cast<qint64> (secs) - NTP_UNIX_OFFSET_S;
    // Convert fractional part to milliseconds.
    qint64 ms_frac = static_cast<qint64> (frac) * 1000LL / 4294967296LL;

    return unix_sec * 1000LL + ms_frac;
  }
}

SntpClient::SntpClient (QObject *parent)
  : QObject {parent}
{
  m_sendTimer.setSingleShot (true);
  m_receiveTimeout.setSingleShot (true);

  connect (&m_socket, &QUdpSocket::readyRead,
           this, &SntpClient::onReadyRead);
  connect (&m_sendTimer, &QTimer::timeout,
           this, &SntpClient::onSendTimer);
  connect (&m_receiveTimeout, &QTimer::timeout,
           this, &SntpClient::onReceiveTimeout);
}

void SntpClient::requestBurst (QString const& server,
                                int count, int spacingMs,
                                int maxRttMs, int recvTimeoutMs)
{
  if (m_busy)
    return; // silently ignore if already running

  m_count         = count;
  m_sent          = 0;
  m_spacingMs     = spacingMs;
  m_maxRttMs      = maxRttMs;
  m_recvTimeoutMs = recvTimeoutMs;
  m_pendingValid  = false;
  m_samples.clear ();
  m_busy          = true;

  // Re-bind socket on each burst so stale data from previous bindings is cleared.
  // Qt 5: unbound socket is in UnconnectedState; BoundState means already bound.
  if (m_socket.state () != QAbstractSocket::UnconnectedState)
    m_socket.close ();
  m_socket.bind (); // any available local port

  QHostInfo::lookupHost (server, this, &SntpClient::onHostResolved);
}

void SntpClient::onHostResolved (QHostInfo const& info)
{
  if (info.error () != QHostInfo::NoError || info.addresses ().isEmpty ())
    {
      m_busy = false;
      Q_EMIT burstComplete (0.0, 0, false);
      return;
    }

  // Prefer IPv4 address when available.
  m_serverAddr = info.addresses ().first ();
  for (auto const& a : info.addresses ())
    if (a.protocol () == QAbstractSocket::IPv4Protocol)
      {
        m_serverAddr = a;
        break;
      }

  sendCurrentRequest ();
}

void SntpClient::sendCurrentRequest ()
{
  // Build 48-byte NTP client request: LI=0, VN=3, Mode=3
  QByteArray pkt (48, '\0');
  pkt[0] = static_cast<char> (0x1B); // 0b00011011

  m_pendingT1    = QDateTime::currentMSecsSinceEpoch ();
  m_pendingValid = true;

  m_socket.writeDatagram (pkt, m_serverAddr, 123);
  m_sent++;

  m_receiveTimeout.start (m_recvTimeoutMs);

  // Schedule the next action: either send the next request or finalize.
  // The spacing timer fires m_spacingMs after this send.
  m_sendTimer.start (m_spacingMs);
}

void SntpClient::onReadyRead ()
{
  while (m_socket.hasPendingDatagrams ())
    {
      QByteArray data (128, '\0');
      qint64 t4Ms = QDateTime::currentMSecsSinceEpoch ();
      qint64 sz   = m_socket.readDatagram (data.data (), data.size ());
      data.resize (static_cast<int> (sz));

      if (!m_pendingValid || data.size () < 48)
        continue;

      Sample s;
      if (parsePacket (data, t4Ms, m_pendingT1, s))
        m_samples.append (s);

      m_pendingValid = false;
      m_receiveTimeout.stop ();
    }
}

void SntpClient::onReceiveTimeout ()
{
  // No response arrived in time; discard pending slot and move on.
  m_pendingValid = false;
  // The send timer will fire next.
}

void SntpClient::onSendTimer ()
{
  if (m_sent < m_count)
    {
      sendCurrentRequest ();
    }
  else
    {
      // All requests sent; finalize (last response may have arrived in onReadyRead).
      finalizeBurst ();
    }
}

void SntpClient::advanceOrFinalize ()
{
  // Not actually needed in this sequential design; kept for clarity.
}

void SntpClient::finalizeBurst ()
{
  m_sendTimer.stop ();
  m_receiveTimeout.stop ();
  m_pendingValid = false;
  m_busy         = false;

  if (m_samples.isEmpty ())
    {
      Q_EMIT burstComplete (0.0, 0, false);
      return;
    }

  // Best sample: smallest RTT, within maxRttMs.
  Sample best;
  int bestRtt = INT_MAX;
  for (auto const& s : qAsConst (m_samples))
    {
      if (s.rtt <= m_maxRttMs && s.rtt < bestRtt)
        {
          best    = s;
          bestRtt = s.rtt;
        }
    }

  if (bestRtt == INT_MAX)
    {
      // All samples exceeded maxRttMs.
      Q_EMIT burstComplete (0.0, 0, false);
    }
  else
    {
      Q_EMIT burstComplete (best.theta, best.rtt, true);
    }
}

bool SntpClient::parsePacket (QByteArray const& data, qint64 t4Ms,
                               qint64 t1Ms, Sample& out) const
{
  if (data.size () < 48)
    return false;

  // NTP timestamps in response:
  //   Originate  T1' (bytes 24-31) -- server echoes our transmit time
  //   Receive    T2  (bytes 32-39) -- server received our request
  //   Transmit   T3  (bytes 40-47) -- server sent its response
  qint64 t2Ms = ntpTimestampToMs (data, 32);
  qint64 t3Ms = ntpTimestampToMs (data, 40);

  if (t2Ms == 0 || t3Ms == 0)
    return false; // malformed or Kiss-of-Death packet

  // RTT = (T4 - T1) - (T3 - T2)
  qint64 rttMs = (t4Ms - t1Ms) - (t3Ms - t2Ms);
  if (rttMs < 0)
    rttMs = 0;

  // Clock offset θ = ((T2 - T1) + (T3 - T4)) / 2  (in milliseconds)
  double thetaMs = (static_cast<double> (t2Ms - t1Ms)
                  + static_cast<double> (t3Ms - t4Ms)) / 2.0;

  out.theta = thetaMs / 1000.0;
  out.rtt   = static_cast<int> (rttMs);
  return true;
}
