#pragma once
// ============================================================================
//  Viessmann E3-CAN – UDS/ISO-TP-Client für ESP32 (TWAI)
//
//  Ziel:   ECU 0x680 (HMUMASTER), Antworten auf 0x690
//  Bus:    250 kBit/s, UDS-Service 0x22 (lesen) / 0x2E (schreiben)
//
//  Die DIDs und Byte-Layouts entsprechen open3e (Open3Edatapoints.py) und
//  der Liste in "open3e esp32/dids-heizung.md".
//
//  Kein blockierendes Warten: loop() arbeitet einen Schritt pro Aufruf ab,
//  damit WLAN/API von der Buskommunikation nicht ausgebremst werden.
// ============================================================================

#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>

#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include "esphome/core/optional.h"
#include "driver/twai.h"

namespace vitodens {

static const char *const TAG = "vitodens_can";

static constexpr uint32_t ECU_TX_ID = 0x680;  // Anfragen gehen hierhin
static constexpr uint32_t ECU_RX_ID = 0x690;  // hier antwortet die ECU

// DIAGNOSE: Wenn true, akzeptiert der Controller ALLE Frames und protokolliert
// jede neu gesehene CAN-ID einmal. Damit laesst sich pruefen, welche ECUs auf
// dem angeschlossenen Strang ueberhaupt senden. Fuer den Normalbetrieb false.
static constexpr bool SCAN_IDS = false;
static constexpr uint32_t REQUEST_TIMEOUT_MS = 400;
static constexpr uint32_t GAP_MS = 25;         // Pause zwischen zwei Anfragen
static constexpr uint32_t FAST_PERIOD_MS = 30000;
static constexpr uint32_t SLOW_PERIOD_MS = 300000;
static constexpr uint32_t ERROR_RETRY_MS = 10000;
static constexpr size_t RX_BUF_SIZE = 128;
static constexpr uint8_t MAX_FRAMES_PER_TICK = 16;

// ---------------------------------------------------------------- Hilfsfunktionen
static inline int16_t le_i16(const uint8_t *p) {
  return (int16_t) ((uint16_t) p[0] | ((uint16_t) p[1] << 8));
}
static inline uint16_t le_u16(const uint8_t *p) {
  return (uint16_t) ((uint16_t) p[0] | ((uint16_t) p[1] << 8));
}
// UDS-Adressen (DID, NRC-Bezug) stehen big-endian im Frame
static inline uint16_t be_u16(const uint8_t *p) {
  return (uint16_t) (((uint16_t) p[0] << 8) | (uint16_t) p[1]);
}
static inline int32_t le_i32(const uint8_t *p) {
  return (int32_t) ((uint32_t) p[0] | ((uint32_t) p[1] << 8) |
                    ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24));
}

// open3e-Enum "SensorStates"
static inline const char *sensor_state_str(uint8_t s) {
  switch (s) {
    case 0: return "ok";
    case 1: return "Unterbrechung";
    case 2: return "Kurzschluss";
    case 3: return "Elektrischer Fehler";
    case 4: return "nicht verfuegbar";
    case 5: return "ungueltig";
    default: return "unbekannt";
  }
}

// ---------------------------------------------------------------- Messwerte
struct Values {
  // 268 Vorlauftemperatur (primär, ab Wärmeerzeuger)
  float flow_temp{NAN}, flow_min{NAN}, flow_max{NAN}, flow_avg{NAN};
  std::string flow_state;
  // 527 Vorlauftemperatur Soll
  float flow_setpoint{NAN};
  // 274 Außentemperatur
  float out_temp{NAN}, out_min{NAN}, out_max{NAN}, out_avg{NAN};
  std::string out_state;
  // 331 Abgastemperatur
  float flue_temp{NAN}, flue_min{NAN}, flue_max{NAN}, flue_avg{NAN};
  std::string flue_state;
  // 318 Wasserdruck
  float pressure{NAN}, press_min{NAN}, press_max{NAN}, press_avg{NAN};
  std::string press_state;
  // 1346 Betriebsstatistik
  float op_hours{NAN}, burner_hours{NAN}, burner_starts{NAN};
  // 548 Energieverbrauch Heizung (kWh)
  float energy_today{NAN}, energy_7d{NAN}, energy_month{NAN}, energy_past_month{NAN},
      energy_year{NAN}, energy_past_year{NAN};
  // 544 Gasverbrauch Heizung (m³)
  float gas_today{NAN}, gas_7d{NAN}, gas_month{NAN}, gas_past_month{NAN},
      gas_year{NAN}, gas_past_year{NAN};
  // 1211 Wärmeerzeugung Heizung (kWh)
  float heat_today{NAN}, heat_7d{NAN}, heat_month{NAN}, heat_past_month{NAN},
      heat_year{NAN}, heat_past_year{NAN};
  // 364 Brennerflamme + Ionisationsstrom
  bool flame{false};
  float ionization{NAN};
  // 381 Heizkreispumpe
  bool pump_on{false};
  float pump_target{NAN}, pump_actual{NAN};
  // 880 Heizkurve Mischer 1
  float curve_gradient{NAN}, curve_level{NAN}, curve_basepoint{NAN};
  // 929 Gastyp
  std::string gas_type;
  // 901 Wartung / 902 Störung / 522 Servicetermin
  bool service_required{false};
  int malfunction{0};
  std::string service_date;
  // 265 Fehlerliste
  int error_count{0};
  std::string error_ids;
  // 1165 / 593 / 607 / 602 / 900 Gateway-Diagnose
  bool backend_connected{false};
  std::string gateway_mac, gateway_ip, gateway_net_state;
  float gateway_rssi{NAN};
};

// ---------------------------------------------------------------- Bus-Klasse
class VitodensBus {
 public:
  Values v;

  void begin(int tx_pin, int rx_pin) {
    if (this->started_)
      return;
    this->tx_pin_ = tx_pin;
    this->rx_pin_ = rx_pin;
    ESP_LOGI(TAG, "Initialisiere TWAI (TX %d, RX %d, 250 kbit/s)", tx_pin, rx_pin);

    twai_general_config_t g = TWAI_GENERAL_CONFIG_DEFAULT((gpio_num_t) tx_pin, (gpio_num_t) rx_pin,
                                                          TWAI_MODE_NORMAL);
    g.rx_queue_len = 32;
    g.tx_queue_len = 8;
    g.alerts_enabled = TWAI_ALERT_BUS_OFF | TWAI_ALERT_BUS_ERROR | TWAI_ALERT_ERR_PASS;

    twai_timing_config_t t = TWAI_TIMING_CONFIG_250KBITS();

    // Nur Antworten der Ziel-ECU annehmen (11-Bit-ID 0x690).
    // Im Diagnosemodus alles annehmen, damit fremde IDs sichtbar werden.
    twai_filter_config_t f = {};
    f.single_filter = true;
    if (SCAN_IDS) {
      f.acceptance_code = 0x00000000;
      f.acceptance_mask = 0xFFFFFFFF;
    } else {
      // Antworten aller ECUs annehmen: 0x690, 0x694, 0x698 ... (0x69x)
      f.acceptance_code = (0x690u << 21);
      f.acceptance_mask = ~(0x7F0u << 21);
    }

    if (twai_driver_install(&g, &t, &f) != ESP_OK) {
      ESP_LOGE(TAG, "TWAI-Treiber konnte nicht installiert werden");
      return;
    }
    if (twai_start() != ESP_OK) {
      ESP_LOGE(TAG, "TWAI konnte nicht gestartet werden");
      return;
    }
    this->driver_ok_ = true;
    this->started_ = true;

    uint32_t now = millis();
    for (uint8_t i = 0; i < JOB_COUNT; i++) {
      this->jobs_[i].due = now + 100 + (uint32_t) i * 60;
    }
    ESP_LOGI(TAG, "E3-CAN gestartet (250 kbit/s, TX %d / RX %d)", tx_pin, rx_pin);
  }

  // Wird zyklisch (20 ms) aus dem YAML aufgerufen.
  void loop() {
    if (!this->driver_ok_)
      return;
    uint32_t now = millis();

    this->check_bus_(now);
    this->drain_rx_();

    // Timeout: der Job MUSS als erledigt markiert werden, sonst bleibt er
    // ueberfaellig und wird im 25-ms-Takt erneut angefragt - das blockiert alle
    // anderen Datenpunkte und flutet den Bus.
    if (this->phase_ != Phase::IDLE && (int32_t) (now - this->deadline_) >= 0) {
      if (this->scan_probe_) {
        // Adress-Scan: keine Antwort = Adresse existiert nicht, kein Fehlerlog
        this->scan_probe_ = false;
        this->reset_rx_();
        this->sweep_addr_++;
        return;
      }
      uint16_t did = this->did_;
      Phase ph = this->phase_;
      this->fail_(did, ph == Phase::WAIT_WRITE ? "Timeout beim Schreiben" : "Timeout");
      // Erst den Schreibvorgang bewerten, dann zuruecksetzen - reset_rx_()
      // bricht sonst seinerseits den Schreibvorgang mit "abgebrochen" ab.
      if (this->write_state_ == WriteState::READING) {
        this->write_failed_("keine Antwort auf Lesen");
      } else if (this->write_state_ == WriteState::WRITING) {
        this->write_failed_(ph == Phase::WAIT_WRITE ? "keine Antwort auf Schreiben" : "abgebrochen");
      }
      this->reset_rx_();
      this->job_done_(did, false);
    }
    if (this->phase_ != Phase::IDLE)
      return;
    if ((int32_t) (now - this->last_tx_) < (int32_t) GAP_MS)
      return;

    // Adress-Scan beim Start: alle ECU-Adressen 0x680..0x6EF mit DID 256
    // (BusIdentification, bei E3-Geraeten immer vorhanden) abklopfen.
    if (this->sweep_active_) {
      if (this->sweep_addr_ > 0x6EF) {
        this->sweep_active_ = false;
        this->tx_id_ = ECU_TX_ID;
        this->rx_id_ = ECU_RX_ID;
        ESP_LOGI(TAG, "Adress-Scan beendet, weiter mit 0x680");
        return;
      }
      this->tx_id_ = this->sweep_addr_;
      this->rx_id_ = this->sweep_addr_ + 0x10;
      this->scan_probe_ = true;
      this->start_read_(256);
      return;
    }

    // Offener Schreibwunsch: erst aktuellen Stand lesen (Read-Modify-Write).
    // Mit Wartezeit zwischen den Versuchen, damit ein schweigendes Steuergeraet
    // das normale Polling nicht blockiert.
    if (this->write_state_ == WriteState::NEED_READ &&
        (int32_t) (now - this->write_next_try_) >= 0) {
      this->write_state_ = WriteState::READING;
      this->start_read_(880);
      return;
    }

    int idx = this->next_job_(now);
    if (idx >= 0)
      this->start_read_(this->jobs_[idx].did);
  }

  // --- Datenzustand -------------------------------------------------------
  // ok(): DID wurde seit dem Start mindestens einmal gelesen (Altverhalten).
  bool ok(uint16_t did) const { return this->have_(did); }

  // Fuer Template-Sensoren:
  //  - nie gelesen -> nichts senden ({}), es gibt keinen Wert
  //  - veraltet    -> ebenfalls nichts senden, der letzte Wert bleibt stehen.
  //    Bewusst KEIN NAN: Home Assistant wirft bei Sensoren mit device_class/
  //    state_class einen ValueError ("non-finite value") und protokolliert das
  //    bei jedem Update. Die Aktualitaet zeigt stattdessen "CAN Daten aktuell"
  //    und der Sensor "Datenalter".
  esphome::optional<float> value(uint16_t did, float v) {
    if (!this->have_(did) || !this->fresh_(did))
      return {};
    return v;
  }
  esphome::optional<std::string> text(uint16_t did, const std::string &s) {
    if (!this->have_(did))
      return {};
    if (!this->fresh_(did))
      return std::string("veraltet");
    return s;
  }
  // Binaersensoren: bei veralteten Daten letzten Zustand halten, statt
  // faelschlich "aus" zu melden.
  esphome::optional<bool> flag(uint16_t did, bool b) {
    if (!this->have_(did) || !this->fresh_(did))
      return {};
    return b;
  }
  // Sekunden seit der letzten erfolgreichen Antwort irgendeiner ECU.
  esphome::optional<float> data_age() {
    if (!this->any_ok_)
      return {};
    return (float) ((millis() - this->last_any_ok_ms_) / 1000);
  }
  bool fresh_any() {
    for (uint8_t i = 0; i < JOB_COUNT; i++) {
      if (this->fresh_(this->jobs_[i].did))
        return true;
    }
    return false;
  }
  const std::string &write_status() const { return this->write_status_; }
  const std::string &bus_ids() const { return this->bus_ids_; }

  // Adress-Scan manuell starten: klopft 0x680..0x6EF mit DID 256 ab und
  // protokolliert die Antworten in bus_ids().
  void start_scan() {
    this->bus_ids_.clear();
    this->foreign_count_ = 0;
    this->sweep_addr_ = 0x680;
    this->sweep_active_ = true;
    ESP_LOGI(TAG, "Adress-Scan gestartet (0x680..0x6EF)");
  }

  // Heizkurve 1 Steigung setzen (DID 880, Sub-Feld Gradient, Faktor 10).
  void request_curve(float value) {
    if (this->write_state_ == WriteState::WRITING) {
      ESP_LOGW(TAG, "Schreibvorgang laeuft noch - Anfrage ignoriert");
      return;
    }
    int g = (int) lroundf(value * 10.0f);
    if (g < 0) g = 0;
    if (g > 255) g = 255;
    this->write_gradient_ = (uint8_t) g;
    this->write_attempts_ = 0;
    this->write_status_ = "laeuft";
    this->write_next_try_ = millis();  // sofort erster Versuch
    this->write_state_ = WriteState::NEED_READ;
    ESP_LOGD(TAG, "Schreibwunsch Heizkurve: Rohwert %u", this->write_gradient_);
  }
  bool write_pending() const { return this->write_state_ != WriteState::NONE; }

  void recover() {
    if (!this->driver_ok_) {
      this->started_ = false;
      this->begin(this->tx_pin_, this->rx_pin_);
      return;
    }
    twai_status_info_t st;
    if (twai_get_status_info(&st) != ESP_OK)
      return;
    if (st.state == TWAI_STATE_BUS_OFF) {
      twai_initiate_recovery();
    } else if (st.state == TWAI_STATE_STOPPED) {
      twai_start();
    } else {
      twai_stop();
      twai_start();
    }
    this->reset_rx_();
    ESP_LOGI(TAG, "CAN-Bus neu gestartet");
  }

  const char *bus_state() {
    if (!this->driver_ok_)
      return "kein Treiber";
    twai_status_info_t st;
    if (twai_get_status_info(&st) != ESP_OK)
      return "unbekannt";
    switch (st.state) {
      case TWAI_STATE_RUNNING: return "OK";
      case TWAI_STATE_BUS_OFF: return "BUS_OFF";
      case TWAI_STATE_RECOVERING: return "RECOVERING";
      case TWAI_STATE_STOPPED: return "STOPPED";
      default: return "unbekannt";
    }
  }
  uint32_t requests_ok() const { return this->ok_count_; }
  uint32_t requests_err() const { return this->err_count_; }
  const std::string &last_error() const { return this->last_error_; }

 protected:
  enum class Phase : uint8_t { IDLE, WAIT_READ, WAIT_WRITE };
  enum class WriteState : uint8_t { NONE, NEED_READ, READING, WRITING };

  struct Job {
    uint16_t did;
    uint32_t period;
    uint32_t due;
  };

  static constexpr uint8_t JOB_COUNT = 22;
  Job jobs_[JOB_COUNT] = {
      // Messwerte: alle 30 s
      {268, FAST_PERIOD_MS, 0},
      {527, FAST_PERIOD_MS, 0},
      {274, FAST_PERIOD_MS, 0},
      {331, FAST_PERIOD_MS, 0},
      {318, FAST_PERIOD_MS, 0},
      {364, FAST_PERIOD_MS, 0},
      {381, FAST_PERIOD_MS, 0},
      // Statistik / Diagnose: alle 5 min
      {1346, SLOW_PERIOD_MS, 0},
      {548, SLOW_PERIOD_MS, 0},
      {544, SLOW_PERIOD_MS, 0},
      {1211, SLOW_PERIOD_MS, 0},
      {880, SLOW_PERIOD_MS, 0},
      {929, SLOW_PERIOD_MS, 0},
      {901, SLOW_PERIOD_MS, 0},
      {902, SLOW_PERIOD_MS, 0},
      {522, SLOW_PERIOD_MS, 0},
      {1165, SLOW_PERIOD_MS, 0},
      {593, SLOW_PERIOD_MS, 0},
      {607, SLOW_PERIOD_MS, 0},
      {602, SLOW_PERIOD_MS, 0},
      {900, SLOW_PERIOD_MS, 0},
      {265, SLOW_PERIOD_MS, 0},
  };

  bool started_{false};
  bool driver_ok_{false};
  int tx_pin_{0}, rx_pin_{0};

  Phase phase_{Phase::IDLE};
  uint16_t did_{0};
  uint32_t deadline_{0};
  uint32_t last_tx_{0};
  uint32_t last_bus_check_{0};

  WriteState write_state_{WriteState::NONE};
  uint32_t write_next_try_{0};
  uint8_t write_attempts_{0};
  std::string write_status_{"idle"};
  uint8_t write_gradient_{0};
  uint8_t write_payload_[4]{0};

  uint8_t buf_[RX_BUF_SIZE]{};
  uint16_t buf_total_{0};
  uint16_t buf_pos_{0};
  uint8_t buf_seq_{1};
  bool buf_multi_{false};

  std::map<uint16_t, uint32_t> last_ok_;
  bool any_ok_{false};
  uint32_t last_any_ok_ms_{0};

  // Aktuell adressierte ECU (im Normalbetrieb fest 0x680/0x690)
  uint32_t tx_id_{ECU_TX_ID};
  uint32_t rx_id_{ECU_RX_ID};

  // Adress-Scan: standardmaessig AUS (kostet 20 s Bootzeit). Ueber den
  // Button "Bus-Scan starten" oder zum Debuggen einschalten.
  bool sweep_active_{false};
  uint32_t sweep_addr_{0x680};
  bool scan_probe_{false};

  // Diagnose: gesehene fremde CAN-IDs
  static constexpr uint8_t FOREIGN_MAX = 40;
  uint32_t foreign_ids_[FOREIGN_MAX]{};
  uint8_t foreign_count_{0};
  std::string bus_ids_;
  uint32_t ok_count_{0};
  uint32_t err_count_{0};
  std::string last_error_;

  // ------------------------------------------------------- Datenzustand
  bool have_(uint16_t did) const { return this->last_ok_.find(did) != this->last_ok_.end(); }
  uint32_t period_of_(uint16_t did) const {
    for (uint8_t i = 0; i < JOB_COUNT; i++) {
      if (this->jobs_[i].did == did)
        return this->jobs_[i].period;
    }
    return SLOW_PERIOD_MS;
  }
  // Veraltet, wenn 5 Abfragezyklen ohne Erfolg vergangen sind (mind. 2 min).
  bool fresh_(uint16_t did) const {
    auto it = this->last_ok_.find(did);
    if (it == this->last_ok_.end())
      return false;
    uint32_t window = this->period_of_(did) * 5;
    if (window < 120000)
      window = 120000;
    return (int32_t) (millis() - it->second) < (int32_t) window;
  }

  // ------------------------------------------------------------ Scheduler
  int next_job_(uint32_t now) {
    for (uint8_t i = 0; i < JOB_COUNT; i++) {
      if ((int32_t) (now - this->jobs_[i].due) >= 0)
        return (int) i;
    }
    return -1;
  }
  void job_done_(uint16_t did, bool success) {
    uint32_t now = millis();
    for (uint8_t i = 0; i < JOB_COUNT; i++) {
      if (this->jobs_[i].did != did)
        continue;
      this->jobs_[i].due = now + (success ? this->jobs_[i].period : ERROR_RETRY_MS);
      return;
    }
  }

  // Job gezielt in X ms erneut lesen (z. B. als Schreib-Bestaetigung)
  void job_due_(uint16_t did, uint32_t in_ms) {
    uint32_t now = millis();
    for (uint8_t i = 0; i < JOB_COUNT; i++) {
      if (this->jobs_[i].did == did) {
        this->jobs_[i].due = now + in_ms;
        return;
      }
    }
  }

  // ------------------------------------------------------------ Senden
  bool send_frame_(uint32_t id, const uint8_t *data) {
    twai_message_t m = {};
    m.identifier = id;
    m.extd = 0;
    m.rtr = 0;
    m.data_length_code = 8;
    memcpy(m.data, data, 8);
    this->last_tx_ = millis();
    return twai_transmit(&m, pdMS_TO_TICKS(20)) == ESP_OK;
  }

  void start_read_(uint16_t did) {
    uint8_t f[8] = {0};
    f[0] = 0x03;  // ISO-TP Single Frame, 3 Byte Nutzdaten
    f[1] = 0x22;  // UDS ReadDataByIdentifier
    f[2] = (uint8_t) (did >> 8);
    f[3] = (uint8_t) (did & 0xFF);
    if (!this->send_frame_(this->tx_id_, f)) {
      if (this->scan_probe_) {
        this->scan_probe_ = false;
        this->sweep_addr_++;
        return;
      }
      this->fail_(did, "Senden fehlgeschlagen");
      this->job_done_(did, false);
      if (this->write_state_ == WriteState::READING)
        this->write_failed_("Senden fehlgeschlagen");
      return;
    }
    this->did_ = did;
    this->phase_ = Phase::WAIT_READ;
    this->deadline_ = millis() + (this->scan_probe_ ? 150 : REQUEST_TIMEOUT_MS);
    this->buf_total_ = 0;
    this->buf_pos_ = 0;
    this->buf_multi_ = false;
  }

  bool start_write_(uint16_t did, const uint8_t *data, uint8_t len) {
    if (len > 7) {
      this->fail_(did, "Schreiblaenge > 7 Byte nicht unterstuetzt");
      return false;
    }
    uint8_t f[8] = {0};
    f[0] = (uint8_t) (0x00 | (len + 3));  // SF, Nutzdaten = 0x2E + DID + Daten
    f[1] = 0x2E;                          // UDS WriteDataByIdentifier
    f[2] = (uint8_t) (did >> 8);
    f[3] = (uint8_t) (did & 0xFF);
    memcpy(&f[4], data, len);
    if (!this->send_frame_(this->tx_id_, f)) {
      this->fail_(did, "Senden (Write) fehlgeschlagen");
      return false;
    }
    this->did_ = did;
    this->phase_ = Phase::WAIT_WRITE;
    this->deadline_ = millis() + REQUEST_TIMEOUT_MS;
    this->buf_total_ = 0;
    this->buf_pos_ = 0;
    this->buf_multi_ = false;
    return true;
  }

  void send_flow_control_() {
    uint8_t f[8] = {0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    this->send_frame_(this->tx_id_, f);
  }

  // ------------------------------------------------------------ Empfangen
  void drain_rx_() {
    for (uint8_t n = 0; n < MAX_FRAMES_PER_TICK; n++) {
      twai_message_t m;
      if (twai_receive(&m, 0) != ESP_OK)
        return;
      if (m.rtr)
        continue;
      if (m.identifier != this->rx_id_) {
        // Diagnose: fremde IDs einmal protokollieren
        if (SCAN_IDS)
          this->note_foreign_id_(m.identifier, m.data_length_code);
        continue;
      }
      this->handle_frame_(m.data, m.data_length_code);
    }
  }

  // Jede neu gesehene fremde CAN-ID genau einmal ins Log schreiben.
  void note_foreign_id_(uint32_t id, uint8_t len) {
    for (uint8_t i = 0; i < this->foreign_count_; i++) {
      if (this->foreign_ids_[i] == id)
        return;
    }
    if (this->foreign_count_ < FOREIGN_MAX) {
      this->foreign_ids_[this->foreign_count_++] = id;
      char b[32];
      snprintf(b, sizeof(b), "Fremd-ID gesehen: 0x%03X (DLC %u)", (unsigned) id, (unsigned) len);
      this->bus_ids_ = this->bus_ids_.empty() ? std::string(b) : this->bus_ids_ + " | " + b;
      ESP_LOGI(TAG, "%s", b);
    }
  }

  void handle_frame_(const uint8_t *d, uint8_t n) {
    if (n < 1)
      return;
    uint8_t pci = (uint8_t) (d[0] >> 4);

    if (pci == 0x0) {  // Single Frame
      uint8_t len = (uint8_t) (d[0] & 0x0F);
      if (len < 1 || len > 7)
        return;
      this->complete_(d + 1, len);
      return;
    }
    if (pci == 0x1) {  // First Frame
      if (n < 8)
        return;
      uint16_t total = (uint16_t) (((d[0] & 0x0F) << 8) | d[1]);
      if (total < 8 || total > RX_BUF_SIZE) {
        this->reset_rx_();
        return;
      }
      memcpy(this->buf_, d + 2, 6);
      this->buf_pos_ = 6;
      this->buf_total_ = total;
      this->buf_seq_ = 1;
      this->buf_multi_ = true;
      this->send_flow_control_();
      return;
    }
    if (pci == 0x2) {  // Consecutive Frame
      if (!this->buf_multi_ || this->phase_ == Phase::IDLE)
        return;
      uint8_t seq = (uint8_t) (d[0] & 0x0F);
      if (seq != this->buf_seq_) {
        this->fail_(this->did_, "Frame-Reihenfolge verloren");
        this->reset_rx_();
        return;
      }
      this->buf_seq_ = (uint8_t) ((this->buf_seq_ + 1) & 0x0F);
      uint16_t remaining = (uint16_t) (this->buf_total_ - this->buf_pos_);
      uint16_t copy = (remaining < (uint16_t) (n - 1)) ? remaining : (uint16_t) (n - 1);
      memcpy(this->buf_ + this->buf_pos_, d + 1, copy);
      this->buf_pos_ = (uint16_t) (this->buf_pos_ + copy);
      if (this->buf_pos_ >= this->buf_total_) {
        this->complete_(this->buf_, this->buf_total_);
      }
      return;
    }
    // Flow-Control-Frames der Gegenseite ignorieren wir.
  }

  void complete_(const uint8_t *data, uint16_t len) {
    if (this->phase_ == Phase::IDLE)
      return;
    if (len < 3)
      return;

    // Antwort auf einen Adress-Scan: nur protokollieren, nicht auswerten.
    if (this->scan_probe_) {
      this->scan_probe_ = false;
      uint16_t n = (uint16_t) (len - 3);
      if (n > 12)
        n = 12;
      char hex[40];
      size_t pos = 0;
      for (uint16_t i = 0; i < n && pos + 4 < sizeof(hex); i++) {
        pos += (size_t) snprintf(hex + pos, sizeof(hex) - pos, "%02X ", data[3 + i]);
      }
      char b[90];
      snprintf(b, sizeof(b), "ECU 0x%03X antwortet (DID %u): %s", (unsigned) this->tx_id_,
               (unsigned) be_u16(data + 1), hex);
      ESP_LOGI(TAG, "%s", b);
      this->bus_ids_ = this->bus_ids_.empty() ? std::string(b) : this->bus_ids_ + " | " + b;
      this->reset_rx_();
      this->sweep_addr_++;
      return;
    }
    uint16_t did = this->did_;

    // Nur Antworten auf die aktuell angefragte DID akzeptieren. Alles andere
    // (verspaetete Antwort, fremde DID, Response eines anderen Testers) wird
    // ignoriert - die Anfrage laeuft dann regulaer in ihren Timeout, und der
    // Timeout-Pfad markiert den Job als erledigt. Frueher wurde hier
    // abgebrochen, ohne job_done_() zu rufen -> Retry im 25-ms-Takt, sobald
    // der Bus ueberhaupt antwortet.
    if (data[0] == 0x7F) {
      if (be_u16(data + 1) != did)
        return;
    } else if ((data[0] == 0x62 || data[0] == 0x6E) && be_u16(data + 1) == did) {
      // passt
    } else {
      return;
    }

    if (data[0] == 0x7F) {  // Negative Response
      char msg[48];
      snprintf(msg, sizeof(msg), "NRC 0x%02X bei DID 0x%03X", len > 2 ? data[2] : 0, did);
      this->fail_(did, msg);
      if (this->write_state_ == WriteState::READING || this->write_state_ == WriteState::WRITING) {
        this->write_failed_(msg);
      }
      this->job_done_(did, false);  // sonst wird der DID sofort erneut angefragt
      this->reset_rx_();
      return;
    }

    if (this->phase_ == Phase::WAIT_READ) {
      if (data[0] != 0x62) {
        return;  // DID passt, aber falsche Antwortart -> Timeout abwarten
      }
      const uint8_t *payload = data + 3;
      uint16_t plen = (uint16_t) (len - 3);

      // Schreibpfad nur fuer DID 880 - niemals die Bytes eines anderen
      // Datenpunkts in die Heizkurve schreiben.
      if (this->write_state_ == WriteState::READING && did == 880) {
        if (plen < 4) {
          this->fail_(did, "Heizkurve: unerwartete Laenge");
          this->write_failed_("unerwartete Laenge");
          this->job_done_(did, false);
          this->reset_rx_();
          return;
        }
        memcpy(this->write_payload_, payload, 4);
        this->write_payload_[0] = this->write_gradient_;
        this->write_state_ = WriteState::WRITING;
        if (!this->start_write_(880, this->write_payload_, 4)) {
          this->write_failed_("Senden fehlgeschlagen");
          this->job_done_(did, false);
          this->reset_rx_();
        }
        return;
      }

      if (this->decode_(did, payload, plen)) {
        this->last_ok_[did] = millis();
        this->last_any_ok_ms_ = millis();
        this->any_ok_ = true;
        this->ok_count_++;
        this->job_done_(did, true);
      } else {
        this->fail_(did, "unbekanntes Format");
        this->job_done_(did, false);
      }
      this->reset_rx_();
      return;
    }

    // WAIT_WRITE
    if (data[0] == 0x6E && be_u16(data + 1) == did) {
      ESP_LOGI(TAG, "Heizkurve geschrieben (Steigung %.1f)", this->write_gradient_ / 10.0f);
      this->write_state_ = WriteState::NONE;
      this->write_status_ = "ok";
      this->job_done_(did, true);
      // 2 s spaeter erneut lesen: bestaetigt, dass der Wert angekommen ist
      this->job_due_(880, 2000);
    } else {
      this->fail_(did, "Schreiben nicht bestaetigt");
      this->write_failed_("nicht bestaetigt");
      this->job_done_(did, false);
    }
    this->reset_rx_();
  }

  void reset_rx_() {
    // Sicherheitsnetz: ein laufender Schreibvorgang darf hier nicht haengen
    // bleiben - sonst bleibt write_state_ auf READING/WRITING und
    // request_curve() verweigert dauerhaft mit "Schreibvorgang laeuft noch".
    // Tritt z.B. bei recover() oder einem verlorenen Consecutive Frame auf.
    if (this->write_state_ == WriteState::READING || this->write_state_ == WriteState::WRITING)
      this->write_failed_("abgebrochen");
    this->phase_ = Phase::IDLE;
    this->buf_total_ = 0;
    this->buf_pos_ = 0;
    this->buf_multi_ = false;
  }

  void fail_(uint16_t did, const char *reason) {
    this->err_count_++;
    char msg[64];
    snprintf(msg, sizeof(msg), "DID 0x%03X: %s", did, reason);
    this->last_error_ = msg;
    ESP_LOGW(TAG, "%s", msg);
  }

  // Schreibversuch fehlgeschlagen: maximal drei Versuche, danach aufgeben.
  // Sonst blockiert ein schweigendes Steuergeraet das Polling dauerhaft.
  void write_failed_(const char *reason) {
    if (this->write_state_ == WriteState::NONE)
      return;
    if (this->write_attempts_ < 3)
      this->write_attempts_++;
    if (this->write_attempts_ >= 3) {
      this->write_state_ = WriteState::NONE;
      this->write_status_ = std::string("abgebrochen: ") + reason;
      ESP_LOGE(TAG, "Heizkurve nicht geschrieben (%s)", reason);
    } else {
      this->write_state_ = WriteState::NEED_READ;
      this->write_next_try_ = millis() + 2000;
      this->write_status_ = std::string("Versuch ") + std::to_string((int) this->write_attempts_ + 1) + ": " + reason;
      ESP_LOGW(TAG, "Heizkurve: %s", reason);
    }
  }

  void check_bus_(uint32_t now) {
    if ((int32_t) (now - this->last_bus_check_) < 5000)
      return;
    this->last_bus_check_ = now;
    twai_status_info_t st;
    if (twai_get_status_info(&st) != ESP_OK)
      return;
    if (st.state == TWAI_STATE_BUS_OFF) {
      ESP_LOGW(TAG, "Bus-Off erkannt, starte Recovery");
      twai_initiate_recovery();
    } else if (st.state == TWAI_STATE_STOPPED) {
      ESP_LOGW(TAG, "Bus gestoppt, starte neu");
      twai_start();
    }
  }

  // ------------------------------------------------------------ Dekodierung
  bool decode_(uint16_t did, const uint8_t *d, uint16_t n) {
    switch (did) {
      case 268:  // FlowTemperatureSensor: 4x int16 /10 + Status
        if (n < 9) return false;
        this->v.flow_temp = le_i16(d) / 10.0f;
        this->v.flow_min = le_i16(d + 2) / 10.0f;
        this->v.flow_max = le_i16(d + 4) / 10.0f;
        this->v.flow_avg = le_i16(d + 6) / 10.0f;
        this->v.flow_state = sensor_state_str(d[8]);
        return true;

      case 527:  // FlowTemperatureTargetSetpoint: int16 /10
        if (n < 2) return false;
        this->v.flow_setpoint = le_i16(d) / 10.0f;
        return true;

      case 274:  // OutsideTemperatureSensor
        if (n < 9) return false;
        this->v.out_temp = le_i16(d) / 10.0f;
        this->v.out_min = le_i16(d + 2) / 10.0f;
        this->v.out_max = le_i16(d + 4) / 10.0f;
        this->v.out_avg = le_i16(d + 6) / 10.0f;
        this->v.out_state = sensor_state_str(d[8]);
        return true;

      case 331:  // FlueGasTemperatureSensor
        if (n < 9) return false;
        this->v.flue_temp = le_i16(d) / 10.0f;
        this->v.flue_min = le_i16(d + 2) / 10.0f;
        this->v.flue_max = le_i16(d + 4) / 10.0f;
        this->v.flue_avg = le_i16(d + 6) / 10.0f;
        this->v.flue_state = sensor_state_str(d[8]);
        return true;

      case 318:  // WaterPressureSensor (open3e liefert 1.4 bei 1.4 bar)
        if (n < 9) return false;
        this->v.pressure = le_i16(d) / 10.0f;
        this->v.press_min = le_i16(d + 2) / 10.0f;
        this->v.press_max = le_i16(d + 4) / 10.0f;
        this->v.press_avg = le_i16(d + 6) / 10.0f;
        this->v.press_state = sensor_state_str(d[8]);
        return true;

      case 1346:  // HeatEngineStatistical: 3x int32
        if (n < 12) return false;
        this->v.op_hours = (float) le_i32(d);
        this->v.burner_hours = (float) le_i32(d + 4);
        this->v.burner_starts = (float) le_i32(d + 8);
        return true;

      case 548:  // EnergyConsumptionCentralHeating: 6x int32 /10 kWh
        if (n < 24) return false;
        this->v.energy_today = le_i32(d) / 10.0f;
        this->v.energy_7d = le_i32(d + 4) / 10.0f;
        this->v.energy_month = le_i32(d + 8) / 10.0f;
        this->v.energy_past_month = le_i32(d + 12) / 10.0f;
        this->v.energy_year = le_i32(d + 16) / 10.0f;
        this->v.energy_past_year = le_i32(d + 20) / 10.0f;
        return true;

      case 544:  // GasConsumptionCentralHeating: 6x int16 /10 m³
        if (n < 12) return false;
        this->v.gas_today = le_i16(d) / 10.0f;
        this->v.gas_7d = le_i16(d + 2) / 10.0f;
        this->v.gas_month = le_i16(d + 4) / 10.0f;
        this->v.gas_past_month = le_i16(d + 6) / 10.0f;
        this->v.gas_year = le_i16(d + 8) / 10.0f;
        this->v.gas_past_year = le_i16(d + 10) / 10.0f;
        return true;

      case 1211:  // GeneratedCentralHeatingOutput: 6x int32 /10 kWh
        if (n < 24) return false;
        this->v.heat_today = le_i32(d) / 10.0f;
        this->v.heat_7d = le_i32(d + 4) / 10.0f;
        this->v.heat_month = le_i32(d + 8) / 10.0f;
        this->v.heat_past_month = le_i32(d + 12) / 10.0f;
        this->v.heat_year = le_i32(d + 16) / 10.0f;
        this->v.heat_past_year = le_i32(d + 20) / 10.0f;
        return true;

      case 364:  // Flame: State, 2x raw, int16 /100 µA, raw
        if (n < 6) return false;
        this->v.flame = d[0] > 0;
        this->v.ionization = le_i16(d + 3) / 100.0f;
        return true;

      case 381:  // CentralHeatingPump: State, TargetValue %, Actual, raw
        if (n < 4) return false;
        this->v.pump_on = d[0] > 0;
        // open3e: O3EInt8 ohne signed=True -> unsigned
        this->v.pump_target = (float) d[1];
        this->v.pump_actual = (float) d[2];
        return true;

      case 880:  // MixerOneCircuitCentralHeatingCurve: Gradient /10, Level, BasePoint
        if (n < 4) return false;
        this->v.curve_gradient = d[0] / 10.0f;
        this->v.curve_level = (float) (int8_t) d[1];
        this->v.curve_basepoint = (float) le_i16(d + 2);
        return true;

      case 929:  // GasType
        if (n < 1) return false;
        switch (d[0]) {
          case 1: this->v.gas_type = "LLGas"; break;
          case 2: this->v.gas_type = "EGas"; break;
          case 3: this->v.gas_type = "Fluessiggas"; break;
          default: this->v.gas_type = "unbekannt"; break;
        }
        return true;

      case 901:  // ServiceManagerIsRequired
        if (n < 1) return false;
        this->v.service_required = d[0] != 0;
        return true;

      case 902:  // MalfunctionIdentification
        if (n < 1) return false;
        this->v.malfunction = (int) d[0];
        return true;

      case 522: {  // ServiceDateNext: Sdate (dd, mm, 2000+yy) + Status
        if (n < 4) return false;
        char b[16];
        snprintf(b, sizeof(b), "%02u.%02u.%04u", d[0], d[1], 2000 + d[2]);
        this->v.service_date = b;
        return true;
      }

      case 265: {  // ErrorDtcList: uint16 Count + n * 12 Byte
        if (n < 2) return false;
        uint16_t count = le_u16(d);
        this->v.error_count = (int) count;
        this->v.error_ids.clear();
        if (count == 0) {
          this->v.error_ids = "keine";
        } else {
          char b[12];
          for (uint16_t i = 0; i < count && (uint32_t) (2 + i * 12 + 2) <= n; i++) {
            snprintf(b, sizeof(b), "0x%04X", le_u16(d + 2 + i * 12));
            if (!this->v.error_ids.empty())
              this->v.error_ids += ", ";
            this->v.error_ids += b;
          }
        }
        return true;
      }

      case 1165:  // BackendConnectionStatus (3 = verbunden)
        if (n < 1) return false;
        this->v.backend_connected = (d[0] == 3);
        return true;

      case 593: {  // GatewayMac: 6 Byte
        if (n < 6) return false;
        char b[20];
        snprintf(b, sizeof(b), "%02X-%02X-%02X-%02X-%02X-%02X", d[0], d[1], d[2], d[3], d[4], d[5]);
        this->v.gateway_mac = b;
        return true;
      }

      case 607: {  // GatewayRemoteIp: 5x IPv4, erstes = WLAN-IP
        if (n < 4) return false;
        char b[20];
        snprintf(b, sizeof(b), "%u.%u.%u.%u", d[0], d[1], d[2], d[3]);
        this->v.gateway_ip = b;
        return true;
      }

      case 602:  // GatewayRemoteLocalNetworkStatus (3 = verbunden)
        if (n < 1) return false;
        this->v.gateway_net_state = (d[0] == 3) ? "verbunden" : "getrennt";
        return true;

      case 900:  // GatewayRemoteSignalStrength (%)
        if (n < 1) return false;
        this->v.gateway_rssi = (float) d[0];
        return true;

      default:
        return false;
    }
  }
};

}  // namespace vitodens

// Eine Instanz für das ganze Programm. Bewusst keine ESPHome-Global-Variable:
// ESPHome generiert Globals vor dem includes-Block, dort ist der Typ noch
// unbekannt.
namespace vitodens {
inline VitodensBus &bus() {
  static VitodensBus instance;
  return instance;
}
}  // namespace vitodens
