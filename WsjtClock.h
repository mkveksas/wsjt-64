// WsjtClock.h -- optional internal time-compensation service
//
// Provides an optionally NTP-aligned clock used exclusively for:
//   - RX/TX cycle/slot timing (which second in minute, decode scheduling)
//   - Waterfall timestamp labels
//
// All logging, ADIF, and host-time displays continue to use
// QDateTime::currentDateTimeUtc() directly -- completely unaffected.
//
// Usage:
//   // At application startup:
//   WsjtClock::instance().init (m_settings);
//
//   // In cycle-timing code:
//   auto now = WsjtClock::instance().nowWsjtUtc();
//
// Settings are stored under the "TimeComp" group in the supplied QSettings.

#pragma once
#include <QObject>
#include <QDateTime>
#include <QTimer>
#include <QSettings>
#include <QString>

class SntpClient;

class WsjtClock : public QObject
{
  Q_OBJECT

public:
  // Singleton access.  The object lives in the main thread.
  static WsjtClock& instance ();

  // Call once from MainWindow constructor (or early in main()).
  // Reads settings from `settings` (group "TimeComp").
  // If enabled at startup, immediately starts a burst and the diagnostics timer.
  void init (QSettings *settings);

  // ---- Query API -------------------------------------------------------

  bool    enabled ()              const { return m_enabled; }
  double  appliedOffsetSeconds () const { return m_appliedOffset; }
  double  lastNtpDeltaSeconds ()  const { return m_lastNtpDelta; }
  int     lastRttMs ()            const { return m_lastRttMs; }
  bool    ntpOk ()                const { return m_ntpOk; }

  // Returns the NTP server hostname currently configured.
  QString ntpServer ()            const { return m_ntpServer; }

  // Host UTC (never modified).
  QDateTime nowRawUtc () const;

  // Returns compensated time when enabled; host UTC when disabled.
  // Use this ONLY for cycle/slot timing and waterfall labels.
  QDateTime nowWsjtUtc () const;

  // Human-readable status for status-bar display, e.g.
  //   "Time comp: ON  (-6.012 s)"  or  "Time comp: OFF"
  QString statusString () const;

  // Human-readable diagnostics string, e.g.
  //   "NTP Δ: -5.997 s  RTT 34 ms"
  QString diagnosticsString () const;

  // ---- Control API -----------------------------------------------------

  void setEnabled (bool en);

  // Trigger a new burst and update the applied offset.
  // Does nothing if not enabled.
  void forceRecompensateBurst ();

  // Set NTP server and persist to settings.
  void setNtpServer (QString const& server);

Q_SIGNALS:
  // Emitted whenever the status string may have changed
  // (enable toggle, offset update, NTP result).
  void statusChanged ();

  // Emitted after each diagnostics-only NTP poll (every 5 minutes when enabled).
  void diagnosticsUpdated ();

private:
  WsjtClock ();

  void loadSettings ();
  void saveSettings ();

  void startSmoothing ();
  void stopSmoothing ();

  void startDiagnosticsTimer ();
  void stopDiagnosticsTimer ();

  // Apply the modulo-60 offset rule to a raw NTP theta and set m_targetOffset.
  void applyModulo60Offset (double rawThetaSeconds);

  // Called by SntpClient when a recompensation burst finishes.
  void onRecompensateBurstDone (double theta, int rtt, bool ok);

  // Called by SntpClient when a diagnostics-only burst finishes.
  void onDiagBurstDone (double theta, int rtt, bool ok);

  // Timer callbacks.
  void onSmoothTimer ();
  void onDiagTimer ();

  QSettings *m_settings {nullptr};

  bool   m_enabled        {false};
  double m_appliedOffset  {0.0};  // seconds; smoothed
  double m_targetOffset   {0.0};  // seconds; set only on burst
  double m_lastNtpDelta   {0.0};  // seconds; diagnostics only
  int    m_lastRttMs      {0};
  bool   m_ntpOk          {false};

  QString m_ntpServer      {"pool.ntp.org"};
  int     m_burstCount     {5};
  int     m_burstSpacingMs {2000};
  int     m_maxRttMs       {300};
  // Slew limit: max change per smoothing tick (100 ms tick = 50 ms change @500 ms/s).
  // At 500 ms/s a 7-second correction converges in ~14 seconds.
  double  m_maxSlewPerTick {0.05}; // seconds per 100 ms tick (= 500 ms/s slew limit)
  int     m_diagPollMin    {5};

  QTimer m_smoothTimer; // 100 ms periodic; slews appliedOffset toward targetOffset
  QTimer m_diagTimer;   // fires every diagPollMin minutes

  SntpClient *m_burstClient {nullptr};
  SntpClient *m_diagClient  {nullptr};
};
