// SntpClient.h -- lightweight SNTP (RFC 4330) UDP client
//
// Sends a sequential burst of NTP requests to a server and picks the best
// sample (lowest RTT) to report a clock-offset estimate.
//
// Usage:
//   auto* c = new SntpClient (this);
//   connect (c, &SntpClient::burstComplete, this, &MyClass::onBurst);
//   c->requestBurst ("pool.ntp.org", 5, 2000 /*ms spacing*/);
//
// The burstComplete signal is always emitted exactly once per requestBurst call.
// Reentrant calls to requestBurst while a burst is running will be ignored
// (connect only one burst at a time, or guard externally).

#pragma once
#include <QObject>
#include <QUdpSocket>
#include <QHostAddress>
#include <QHostInfo>
#include <QTimer>
#include <QVector>

class SntpClient : public QObject
{
  Q_OBJECT
public:
  explicit SntpClient (QObject *parent = nullptr);

  // Returns true if a burst is currently in progress.
  bool busy () const { return m_busy; }

  // Start a burst of `count` sequential SNTP requests to `server`:port 123.
  // Each request waits up to `receiveTimeoutMs` for a reply; after that or on
  // success, the next request is sent after an additional `spacingMs` delay.
  // Samples with RTT > `maxRttMs` are discarded.
  // Does nothing if already busy.
  void requestBurst (QString const& server,
                     int count          = 5,
                     int spacingMs      = 2000,
                     int maxRttMs       = 300,
                     int recvTimeoutMs  = 1000);

Q_SIGNALS:
  // Emitted when all requests are done (or aborted).
  // success = false means no usable sample was obtained.
  void burstComplete (double thetaSeconds, int rttMs, bool success);

private Q_SLOTS:
  void onHostResolved (QHostInfo const& info);
  void onReadyRead ();
  void onReceiveTimeout ();
  void onSendTimer ();

private:
  struct Sample
  {
    double theta {0.0}; // clock offset in seconds (positive = NTP ahead of host)
    int    rtt   {0};   // round-trip time in milliseconds
  };

  void sendCurrentRequest ();
  void advanceOrFinalize ();
  void finalizeBurst ();
  bool parsePacket (QByteArray const& data, qint64 t4Ms, qint64 t1Ms,
                    Sample& out) const;

  QUdpSocket m_socket;
  QTimer     m_sendTimer;       // fires spacingMs after each exchange; drives next send
  QTimer     m_receiveTimeout;  // fires recvTimeoutMs after each send

  QHostAddress m_serverAddr;
  int  m_count         {0};
  int  m_sent          {0};
  int  m_spacingMs     {2000};
  int  m_maxRttMs      {300};
  int  m_recvTimeoutMs {1000};

  qint64 m_pendingT1   {0};
  bool   m_pendingValid{false};
  bool   m_busy        {false};

  QVector<Sample> m_samples;
};
