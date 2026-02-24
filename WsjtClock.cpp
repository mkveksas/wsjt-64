// WsjtClock.cpp
#include "WsjtClock.h"
#include "Network/SntpClient.h"

#include <QDebug>
#include <cmath>
#include <algorithm>

// ---------------------------------------------------------------------------
//  Singleton
// ---------------------------------------------------------------------------

WsjtClock& WsjtClock::instance ()
{
  static WsjtClock s_instance;
  return s_instance;
}

WsjtClock::WsjtClock ()
{
  m_smoothTimer.setInterval (100);  // 100 ms tick
  connect (&m_smoothTimer, &QTimer::timeout, this, &WsjtClock::onSmoothTimer);

  m_diagTimer.setSingleShot (false);
  connect (&m_diagTimer, &QTimer::timeout, this, &WsjtClock::onDiagTimer);
}

// ---------------------------------------------------------------------------
//  Init
// ---------------------------------------------------------------------------

void WsjtClock::init (QSettings *settings)
{
  m_settings = settings;
  loadSettings ();

  // Create SNTP client objects (one for recompensation bursts, one for diags).
  m_burstClient = new SntpClient (this);
  m_diagClient  = new SntpClient (this);

  connect (m_burstClient, &SntpClient::burstComplete,
           this, &WsjtClock::onRecompensateBurstDone);
  connect (m_diagClient,  &SntpClient::burstComplete,
           this, &WsjtClock::onDiagBurstDone);

  if (m_enabled)
    {
      forceRecompensateBurst ();
      startDiagnosticsTimer ();
    }
}

// ---------------------------------------------------------------------------
//  Settings persistence
// ---------------------------------------------------------------------------

void WsjtClock::loadSettings ()
{
  if (!m_settings) return;
  m_settings->beginGroup ("TimeComp");
  m_enabled        = m_settings->value ("Enabled",          false).toBool ();
  m_ntpServer      = m_settings->value ("NtpServer",        "pool.ntp.org").toString ();
  m_burstCount     = m_settings->value ("BurstCount",       5).toInt ();
  m_burstSpacingMs = m_settings->value ("BurstSpacingMs",   2000).toInt ();
  m_maxRttMs       = m_settings->value ("MaxRttMs",         300).toInt ();
  m_diagPollMin    = m_settings->value ("DiagPollMinutes",  5).toInt ();
  // maxSlewMsPerSec → convert to per-100ms-tick.
  // Default 500 ms/s: a 7-second correction converges in ~14 s while still
  // preventing single-tick time jumps larger than 50 ms.
  int slewMsPerSec = m_settings->value ("MaxSlewMsPerSec", 500).toInt ();
  m_maxSlewPerTick = slewMsPerSec * 0.1 / 1000.0; // ms/s * 0.1s / 1000 = s per tick
  m_settings->endGroup ();
}

void WsjtClock::saveSettings ()
{
  if (!m_settings) return;
  m_settings->beginGroup ("TimeComp");
  m_settings->setValue ("Enabled",        m_enabled);
  m_settings->setValue ("NtpServer",      m_ntpServer);
  m_settings->setValue ("BurstCount",     m_burstCount);
  m_settings->setValue ("BurstSpacingMs", m_burstSpacingMs);
  m_settings->setValue ("MaxRttMs",       m_maxRttMs);
  m_settings->setValue ("DiagPollMinutes",m_diagPollMin);
  m_settings->endGroup ();
}

// ---------------------------------------------------------------------------
//  Query
// ---------------------------------------------------------------------------

QDateTime WsjtClock::nowRawUtc () const
{
  return QDateTime::currentDateTimeUtc ();
}

QDateTime WsjtClock::nowWsjtUtc () const
{
  if (!m_enabled || m_appliedOffset == 0.0)
    return QDateTime::currentDateTimeUtc ();

  return QDateTime::currentDateTimeUtc ()
         .addMSecs (qRound (m_appliedOffset * 1000.0));
}

QString WsjtClock::statusString () const
{
  if (!m_enabled)
    return tr ("Time comp: OFF");

  return tr ("Time comp: ON  (%1 s)")
         .arg (m_appliedOffset, 0, 'f', 3);
}

QString WsjtClock::diagnosticsString () const
{
  if (!m_enabled)
    return {};

  if (!m_ntpOk)
    return tr ("NTP: unavailable");

  return tr ("NTP \u0394: %1 s  RTT %2 ms")
         .arg (m_lastNtpDelta, 0, 'f', 3)
         .arg (m_lastRttMs);
}

// ---------------------------------------------------------------------------
//  Control
// ---------------------------------------------------------------------------

void WsjtClock::setEnabled (bool en)
{
  if (m_enabled == en) return;

  m_enabled = en;
  saveSettings ();

  if (en)
    {
      forceRecompensateBurst ();
      startDiagnosticsTimer ();
    }
  else
    {
      stopSmoothing ();
      stopDiagnosticsTimer ();
      m_appliedOffset = 0.0;
      m_targetOffset  = 0.0;
      m_ntpOk         = false;
      Q_EMIT statusChanged ();
    }
}

void WsjtClock::forceRecompensateBurst ()
{
  if (!m_enabled) return;
  if (!m_burstClient || m_burstClient->busy ()) return;

  qDebug () << "WsjtClock: starting recompensation burst on" << m_ntpServer;
  m_burstClient->requestBurst (m_ntpServer, m_burstCount, m_burstSpacingMs,
                                m_maxRttMs);
}

void WsjtClock::setNtpServer (QString const& server)
{
  if (m_ntpServer == server) return;
  m_ntpServer = server;
  saveSettings ();
}

// ---------------------------------------------------------------------------
//  SNTP callbacks
// ---------------------------------------------------------------------------

void WsjtClock::onRecompensateBurstDone (double theta, int rtt, bool ok)
{
  if (!ok)
    {
      qDebug () << "WsjtClock: recompensation burst failed (NTP unreachable or RTT too high)";
      m_ntpOk = false;
      Q_EMIT statusChanged ();
      return;
    }

  m_ntpOk      = true;
  m_lastRttMs  = rtt;

  applyModulo60Offset (theta);

  qDebug () << "WsjtClock: burst done: raw theta=" << theta
            << "s  target=" << m_targetOffset << "s  rtt=" << rtt << "ms";

  startSmoothing ();
  Q_EMIT statusChanged ();
}

void WsjtClock::onDiagBurstDone (double theta, int rtt, bool ok)
{
  // Diagnostics poll: update display fields ONLY; never change target/applied.
  if (ok)
    {
      m_lastNtpDelta = theta;
      m_lastRttMs    = rtt;
      m_ntpOk        = true;
    }
  else
    {
      m_ntpOk = false;
    }

  Q_EMIT diagnosticsUpdated ();
  Q_EMIT statusChanged ();
}

// ---------------------------------------------------------------------------
//  Modulo-60 offset computation
// ---------------------------------------------------------------------------

void WsjtClock::applyModulo60Offset (double rawThetaSeconds)
{
  // Keep only the within-minute component so cycle slots still line up.
  double delta = std::fmod (rawThetaSeconds, 60.0);
  if (delta >= +30.0) delta -= 60.0;
  if (delta <  -30.0) delta += 60.0;

  m_targetOffset   = delta;
  m_lastNtpDelta   = rawThetaSeconds;
}

// ---------------------------------------------------------------------------
//  Smoothing timer
// ---------------------------------------------------------------------------

void WsjtClock::startSmoothing ()
{
  if (!m_smoothTimer.isActive ())
    m_smoothTimer.start ();
}

void WsjtClock::stopSmoothing ()
{
  m_smoothTimer.stop ();
  m_appliedOffset = 0.0;
  m_targetOffset  = 0.0;
}

void WsjtClock::onSmoothTimer ()
{
  double diff = m_targetOffset - m_appliedOffset;
  if (std::abs (diff) < 0.0001)
    {
      m_appliedOffset = m_targetOffset;
      return;
    }

  // Clamp step to max slew rate per tick.
  double step = diff > 0.0
    ? std::min ( diff,  m_maxSlewPerTick)
    : std::max ( diff, -m_maxSlewPerTick);

  m_appliedOffset += step;
  Q_EMIT statusChanged ();
}

// ---------------------------------------------------------------------------
//  Diagnostics timer
// ---------------------------------------------------------------------------

void WsjtClock::startDiagnosticsTimer ()
{
  int intervalMs = m_diagPollMin * 60 * 1000;
  m_diagTimer.setInterval (intervalMs);
  if (!m_diagTimer.isActive ())
    m_diagTimer.start ();
}

void WsjtClock::stopDiagnosticsTimer ()
{
  m_diagTimer.stop ();
}

void WsjtClock::onDiagTimer ()
{
  if (!m_enabled || !m_diagClient || m_diagClient->busy ()) return;

  // Single-sample mini-burst (count=2) for diagnostics; does NOT update target.
  m_diagClient->requestBurst (m_ntpServer, 2, 1000, m_maxRttMs);
}
