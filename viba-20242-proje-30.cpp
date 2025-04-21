// veri3.cpp — VİBA‑20242 Data‑Link katmanı ödevi / Qt Widgets tek‑dosya uygulaması
// -----------------------------------------------------------------------------
// Build:  cmake -B build -S . && cmake --build build -j
// CMakeLists.txt:  set(CMAKE_AUTOMOC ON)  +  find_package(Qt6 COMPONENTS Widgets REQUIRED)

#include <QtWidgets>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <functional>
#include <cstdarg>
#include <QThread>

/* ===============================================================
   0.  Thread‑safe GUI logger
   =============================================================== */
static std::function<void(const QString &)> g_post;   // GUI thread’ine kuyruklar

static void gui_log(const QString &msg)
{
    if (g_post)
        g_post(msg);
}

static void gui_printf(const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    gui_log(QString::fromUtf8(buf));
}

#define printf gui_printf          // legacy printf → GUI
#define puts(s) gui_log(QString::fromUtf8(s))

/* ===============================================================
   1.  Sabitler / veri yapıları
   =============================================================== */
constexpr int MAX_BITS       = 900000;
constexpr int PAYLOAD_BITS   = 100;
constexpr int MAX_FRAMES     = MAX_BITS / PAYLOAD_BITS + 8;
constexpr int MAX_FRAME_BITS = 256;

constexpr int P_LOSS_DATA   = 10;
constexpr int P_CORRUPT     = 20;
constexpr int P_LOSS_ACK    = 15;
constexpr int P_CORRUPT_CHK = 5;
constexpr int MAX_RETRY     = 100;

constexpr uint8_t  FLAG_BYTE   = 0x7E;
constexpr uint8_t  SENDER_ADDR = 0x01;
constexpr uint8_t  RECV_ADDR   = 0x02;
constexpr uint8_t  CTRL_CHKSUM = 0xCC;
constexpr uint16_t CRC_POLY    = 0x1021;
constexpr uint16_t CRC_INIT    = 0xFFFF;

static inline int rnd100() { return std::rand() % 100; }

static inline uint8_t bitsToByte(const int *b)
{
    uint8_t v = 0;
    for (int i = 0; i < 8; ++i)
        v = (v << 1) | b[i];
    return v;
}

static inline uint16_t bitsToU16(const int *b)
{
    uint16_t v = 0;
    for (int i = 0; i < 16; ++i)
        v = (v << 1) | b[i];
    return v;
}

struct Frame
{
    int bits[MAX_FRAME_BITS]{};
    int len { 0 };
};

static int      srcBits   [MAX_BITS];
static long     srcLen         = 0;
static Frame    frames    [MAX_FRAMES];
static uint16_t frameCRC  [MAX_FRAMES];
static uint8_t  crcBytes  [MAX_FRAMES * 2];
static int      crcByteCnt     = 0;

/* ===============================================================
   2.  CRC / checksum / HDLC‑stuffing
   =============================================================== */
uint16_t crc16(const int *b, int n)
{
    uint16_t crc = CRC_INIT;
    for (int i = 0; i < n; ++i)
    {
        bool xorBit = ((crc >> 15) & 1U) ^ (b[i] & 1U);
        crc <<= 1U;
        if (xorBit) crc ^= CRC_POLY;
        crc &= 0xFFFF;
    }
    return crc;
}

uint8_t checksum8(const uint8_t *arr, int n, uint16_t *rawSum = nullptr)
{
    uint16_t sum = 0;
    for (int i = 0; i < n; ++i)
    {
        sum += arr[i];
        if (sum > 0xFF) sum = (sum & 0xFF) + 1;
    }
    if (rawSum) *rawSum = sum + 1;
    return static_cast<uint8_t>(~sum);
}

void pushBits(Frame &f, uint32_t val, int n)
{
    for (int i = n - 1; i >= 0; --i)
        f.bits[f.len++] = (val >> i) & 1U;
}

void buildDataFrame(const int payload[PAYLOAD_BITS], uint16_t crc, uint16_t seq, Frame &f)
{
    f.len = 0;
    pushBits(f, FLAG_BYTE,   8);
    pushBits(f, SENDER_ADDR, 8);
    pushBits(f, RECV_ADDR,   8);
    pushBits(f, seq,        16);
    for (int i = 0; i < PAYLOAD_BITS; ++i)
        f.bits[f.len++] = payload[i];
    pushBits(f, crc,        16);
    pushBits(f, FLAG_BYTE,   8);
}

void buildChecksumFrame(uint8_t chk, Frame &f)
{
    f.len = 0;
    pushBits(f, FLAG_BYTE,   8);
    pushBits(f, SENDER_ADDR, 8);
    pushBits(f, RECV_ADDR,   8);
    pushBits(f, CTRL_CHKSUM, 8);
    pushBits(f, chk,         8);
    pushBits(f, FLAG_BYTE,   8);
}

void stuff(Frame &f)
{
    int tmp[MAX_FRAME_BITS], out = 0;
    for (int i = 0; i < 8; ++i) tmp[out++] = f.bits[i];
    int ones = 0;
    for (int i = 8; i < f.len - 8; ++i)
    {
        int b = f.bits[i];
        tmp[out++] = b;
        if (b && ++ones == 5) { tmp[out++] = 0; ones = 0; }
        else if (!b) ones = 0;
    }
    for (int i = f.len - 8; i < f.len; ++i) tmp[out++] = f.bits[i];
    std::copy(tmp, tmp + out, f.bits);
    f.len = out;
}

/* ===============================================================
   3.  Worker thread – FrameSimulator
   =============================================================== */
struct Stats
{
    int totalTries  = 0;
    int lost        = 0;
    int corrupt     = 0;
    int lostAck     = 0;
    int corruptChk  = 0;
    int maxT        = 0;
    int maxF        = 0;
};

class FrameSimulator : public QObject
{
    Q_OBJECT
public:
    explicit FrameSimulator(QObject *p = nullptr) : QObject(p) {}

    bool loadFile(const QString &path)
    {
        QByteArray local = QFile::encodeName(path);
        srcLen     = 0;
        crcByteCnt = 0;
        return readBits(local.constData());
    }

signals:
    void crcReady(int seq, uint16_t crc);
    void checksumReady(uint8_t chk, uint16_t raw);
    void flowUpdate(const QString &side, const QString &msg);
    void summaryReady(const Stats &st);
    void finished();

public slots:
    void run()
    {
        int payload[PAYLOAD_BITS], frameCnt = 0;
        // CRC’leri hesapla
        for (long p = 0; p < srcLen; p += PAYLOAD_BITS)
        {
            for (int i = 0; i < PAYLOAD_BITS; ++i)
                payload[i] = (p + i < srcLen) ? srcBits[p + i] : 0;
            uint16_t crc = crc16(payload, PAYLOAD_BITS);
            frameCRC[frameCnt]     = crc;
            crcBytes[crcByteCnt++] = crc >> 8;
            crcBytes[crcByteCnt++] = crc & 0xFF;
            buildDataFrame(payload, crc, frameCnt, frames[frameCnt]);
            stuff(frames[frameCnt]);
            emit crcReady(frameCnt, crc);
            ++frameCnt;
        }
        // Checksum frame
        uint16_t raw; uint8_t chk = checksum8(crcBytes, crcByteCnt, &raw);
        buildChecksumFrame(chk, frames[frameCnt]);
        stuff(frames[frameCnt]);
        ++frameCnt;
        emit checksumReady(chk, raw);

        // Stop‑&‑Wait simülasyonu
        Stats st;
        gui_log("\n=== Stop‑&‑Wait Simülasyonu ===");
        std::srand((unsigned)std::time(nullptr));
        for (int idx = 0; idx < frameCnt; )
        {
            bool isChk = (idx == frameCnt - 1);
            int  tries = 0;
            while (tries < MAX_RETRY)
            {
                ++tries;
                emit flowUpdate("sender", tr("Frame %1 gönderiliyor").arg(idx));
                QThread::msleep(20);
                // kaybolma
                if (!isChk && rnd100() < P_LOSS_DATA)
                {
                    ++st.lost;
                    emit flowUpdate("sender", tr("Frame %1 KAYBOLDU").arg(idx));
                    QThread::msleep(20);
                    continue;
                }
                // bozulma
                if (!isChk && rnd100() < P_CORRUPT)
                {
                    ++st.corrupt;
                    emit flowUpdate("sender", tr("Frame %1 BOZULDU").arg(idx));
                    QThread::msleep(20);
                }
                if (isChk && rnd100() < P_CORRUPT_CHK)
                {
                    ++st.corruptChk;
                    emit flowUpdate("sender", "Checksum frame BOZULDU");
                    QThread::msleep(20);
                }
                emit flowUpdate("receiver", tr("Frame %1 alındı").arg(idx));
                QThread::msleep(20);
                // ACK kaybı
                if (rnd100() < P_LOSS_ACK)
                {
                    ++st.lostAck;
                    emit flowUpdate("receiver", tr("ACK kayıp (Frame %1)").arg(idx));
                    QThread::msleep(20);
                    continue;
                }
                emit flowUpdate("sender", tr("ACK geldi (Frame %1)").arg(idx));
                QThread::msleep(20);
                ++idx;
                break;
            }
            st.totalTries += tries;
            if (tries > st.maxT) { st.maxT = tries; st.maxF = idx; }
        }
        emit summaryReady(st);
        emit finished();
    }

private:
    bool readBits(const char *fname)
    {
        FILE *fp = std::fopen(fname, "rb");
        if (!fp) { perror(fname); return false; }
        int ch;
        while ((ch = std::fgetc(fp)) != EOF)
            for (int i = 7; i >= 0; --i)
                srcBits[srcLen++] = (ch >> i) & 1;
        std::fclose(fp);
        return true;
    }
};

/* ===============================================================
   4.  MainWindow – GUI
   =============================================================== */
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow()
    {
        auto *w = new QWidget; setCentralWidget(w);
        auto *v = new QVBoxLayout(w);

        // Dosya seç
        auto *h = new QHBoxLayout;
        path      = new QLineEdit; path->setReadOnly(true);
        auto *btnBrowse = new QPushButton("Gözat…");
        h->addWidget(path); h->addWidget(btnBrowse);
        v->addLayout(h);

        // Simülasyonu başlat
        auto *btnRun = new QPushButton("Simülasyonu başlat");
        v->addWidget(btnRun);

        // CRC tablosu
        table = new QTableWidget(0,2);
        table->setHorizontalHeaderLabels({"Frame","CRC"});
        table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        table->setMinimumHeight(200);
        v->addWidget(table,2);

        // Akış listeleri
        auto *flowLay = new QHBoxLayout;
        sender   = new QListWidget;
        receiver = new QListWidget;
        sender->setMinimumWidth(200);
        receiver->setMinimumWidth(200);
        flowLay->addWidget(sender);
        flowLay->addWidget(receiver);
        v->addLayout(flowLay,1);

        // Durum LED’i
        auto *ledLay = new QHBoxLayout;
        ledLay->addWidget(new QLabel("Durum:"));
        statusLed = new QLabel;
        statusLed->setFixedSize(30,30);  // biraz daha büyük
        statusLed->setStyleSheet("background-color:gray;border-radius:15px;");
        ledLay->addWidget(statusLed);
        v->addLayout(ledLay);

        // Checksum
        chkLabel = new QLabel("CHECKSUM: -");
        v->addWidget(chkLabel);

        // Özet
        summary = new QPlainTextEdit; summary->setReadOnly(true);
        v->addWidget(summary,2);

        // Mesajı göster butonu
        btnShowMsg = new QPushButton("Mesajı Göster");
        btnShowMsg->setEnabled(false);
        v->addWidget(btnShowMsg);

        // Bağlantılar
        connect(btnBrowse, &QPushButton::clicked, this, &MainWindow::chooseFile);
        connect(btnRun,    &QPushButton::clicked, this, &MainWindow::startSim);
        connect(btnShowMsg,&QPushButton::clicked, this, &MainWindow::showDecodedData);

        // Logger
        g_post = [this](const QString &s){
            QMetaObject::invokeMethod(summary,
                [=]{ summary->appendPlainText(s); },
                Qt::QueuedConnection);
        };
    }

private slots:
    void chooseFile()
    {
        QString fn = QFileDialog::getOpenFileName(
            this,".dat seçiniz",QDir::homePath(),
            "Veri Dosyaları (*.dat);;Tüm Dosyalar (*.*)"
        );
        if(!fn.isEmpty()) path->setText(fn);
    }

    void startSim()
    {
        if(path->text().isEmpty()) return;

        // reset UI
        table->setRowCount(0);
        sender->clear(); receiver->clear();
        summary->clear();
        chkLabel->setText("CHECKSUM: -");
        statusLed->setStyleSheet("background-color:gray;border-radius:15px;");
        btnShowMsg->setEnabled(false);

        auto *sim = new FrameSimulator;
        if(!sim->loadFile(path->text())){
            QMessageBox::warning(this,"Hata","Dosya açılamadı");
            delete sim; return;
        }

        QThread *t = new QThread(this);
        sim->moveToThread(t);

        connect(t,      &QThread::started,      sim, &FrameSimulator::run);
        connect(sim,    &FrameSimulator::finished,t,   &QThread::quit);
        connect(sim,    &FrameSimulator::finished,sim,&QObject::deleteLater);
        connect(t,      &QThread::finished,     t,    &QObject::deleteLater);

        connect(sim,    &FrameSimulator::crcReady,     this, &MainWindow::addCrcRow,   Qt::QueuedConnection);
        connect(sim,    &FrameSimulator::checksumReady,this, &MainWindow::showChecksum,Qt::QueuedConnection);
        connect(sim,    &FrameSimulator::flowUpdate,   this, &MainWindow::updateFlow,  Qt::QueuedConnection);
        connect(sim,    &FrameSimulator::summaryReady, this, &MainWindow::showSummary, Qt::QueuedConnection);

        // enable “Mesajı Göster” once done
        connect(sim,    &FrameSimulator::finished,     this, [this](){
            btnShowMsg->setEnabled(true);
        }, Qt::QueuedConnection);

        t->start();
    }

    void addCrcRow(int seq,uint16_t crc)
    {
        int r = table->rowCount();
        table->insertRow(r);
        table->setItem(r,0,new QTableWidgetItem(QString::number(seq)));
        table->setItem(r,1,new QTableWidgetItem(
            QString("0x%1").arg(crc,4,16,QChar('0')).toUpper()));
    }

    void showChecksum(uint8_t chk,uint16_t raw)
    {
        chkLabel->setText(
            QString("CHECKSUM: 0x%1 (raw wrap 0x%2)")
                .arg(chk,2,16,QChar('0')).toUpper()
                .arg(raw,2,16,QChar('0')).toUpper()
        );
    }

    void updateFlow(const QString &side,const QString &txt)
    {
        if (txt.contains("KAYBOLDU")||txt.contains("BOZULDU")||txt.contains("ACK kayıp"))
            statusLed->setStyleSheet("background-color:red;border-radius:15px;");
        else if (txt.contains("ACK geldi"))
            statusLed->setStyleSheet("background-color:green;border-radius:15px;");

        (side=="sender"?sender:receiver)->addItem(txt);
    }

    void showSummary(const Stats &s)
    {
        summary->appendPlainText("\n=== ÖZET ===");
        summary->appendPlainText(QString("Toplam frame           : %1").arg(table->rowCount()));
        summary->appendPlainText(QString("Toplam TRY             : %1").arg(s.totalTries));
        summary->appendPlainText(QString("En çok TRY frame=%1 (try=%2)").arg(s.maxF).arg(s.maxT));
        summary->appendPlainText(QString("Kaybolan frame sayısı  : %1").arg(s.lost));
        summary->appendPlainText(QString("Bozulan frame sayısı   : %1").arg(s.corrupt));
        summary->appendPlainText(QString("Kayıp ACK sayısı       : %1").arg(s.lostAck));
        summary->appendPlainText(QString("Bozuk checksum frame   : %1").arg(s.corruptChk));
    }

    void showDecodedData()
    {
        // reconstruct and display the original .dat contents
        QByteArray raw;
        for (long i = 0; i < srcLen; i += 8) {
            int b = 0;
            for (int j = 0; j < 8 && i + j < srcLen; ++j)
                b = (b << 1) | srcBits[i + j];
            raw.append(char(b));
        }
        QDialog dlg(this);
        dlg.setWindowTitle("Decoded .dat Contents");
        dlg.resize(600,400);
        auto *te = new QTextEdit(&dlg);
        te->setReadOnly(true);
        te->setPlainText(QString::fromLatin1(raw));
        auto *lay = new QVBoxLayout(&dlg);
        lay->addWidget(te);
        dlg.exec();
    }

private:
    QLineEdit      *path      {nullptr};
    QTableWidget   *table     {nullptr};
    QListWidget    *sender    {nullptr};
    QListWidget    *receiver  {nullptr};
    QLabel         *chkLabel  {nullptr};
    QLabel         *statusLed {nullptr};
    QPlainTextEdit *summary   {nullptr};
    QPushButton    *btnShowMsg{nullptr};
};

#include "viba-20242-proje-30.moc"

int main(int argc,char **argv)
{
    QApplication app(argc,argv);
    MainWindow w;
    w.setWindowTitle("VİBA‑20242 Data‑Link Simülatörü");
    w.resize(950,680);
    w.show();
    return app.exec();
}
