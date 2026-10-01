#include "pyrowavecalibrator.h"
#include "pyrowavebandwidth.h"
#include "networkbuffers.h"
#include "computermanager.h"
#include "nvcomputer.h"
#include "streaming/session.h"
#include <QHostInfo>
#include <QReadLocker>
#include <array>
#include <limits>

PyroWaveCalibrator::~PyroWaveCalibrator() {
    if (m_Worker) { m_Worker->requestInterruption(); m_Worker->wait(); delete m_Worker; }
}
void PyroWaveCalibrator::cancel() { if (m_Worker) m_Worker->requestInterruption(); }
QVariantList PyroWaveCalibrator::hosts(ComputerManager* manager) {
    QVariantList result;
    if (!manager) return result;
    for (const auto* pc : manager->getComputers()) {
        QReadLocker lock(&pc->lock);
        if (pc->state == NvComputer::CS_ONLINE && pc->pairState == NvComputer::PS_PAIRED &&
            (pc->serverCodecModeSupport & SCM_MASK_PYROWAVE))
            result.append(QVariantMap{{"name", pc->name}, {"uuid", pc->uuid}, {"address", pc->activeAddress.address()}});
    }
    return result;
}
void PyroWaveCalibrator::start(ComputerManager* manager, const QString& uuid) {
    if (m_Running) return;
    m_Ceiling = 0;
    if (Session::get() != nullptr) { m_Message = tr("End the stream before calibrating."); emit changed(); return; }
    NvAddress address;
    QSslCertificate certificate;
    uint16_t port = 0;
    bool trueUid = true;
    if (manager) for (const auto* pc : manager->getComputers()) {
        QReadLocker lock(&pc->lock);
        if (pc->uuid != uuid || pc->state != NvComputer::CS_ONLINE ||
            pc->pairState != NvComputer::PS_PAIRED || !(pc->serverCodecModeSupport & SCM_MASK_PYROWAVE)) continue;
        address = pc->activeAddress; certificate = pc->serverCert;
        port = pc->activeHttpsPort; trueUid = !pc->isNvidiaServerSoftware; break;
    }
    if (address.isNull() || certificate.isNull() || port == 0) {
        m_Message = tr("Select an online, paired PyroWave host."); emit changed(); return;
    }
    if (m_Worker) { m_Worker->wait(); delete m_Worker; }
    m_Running = true;
    m_Message = tr("Testing host → client bandwidth (one warm-up and three measurements)…"); emit changed();
    m_Worker = QThread::create([this, address, certificate, port, trueUid] {
        QString message;
        int ceiling = 0;
        try {
            NvHTTP http(address, port, certificate, trueUid);
            const QString info = http.getServerInfo(NvHTTP::NVLL_ERROR);
            if (NvHTTP::getXmlString(info, "PyroWaveBandwidthProbeBytes").toLongLong() != PyroWaveBandwidth::ProbeBytes)
                throw std::runtime_error("This host does not support the 32 MiB PyroWave bandwidth probe.");
            if (!(NvHTTP::getXmlString(info, "ServerCodecModeSupport").toUInt() & SCM_MASK_PYROWAVE) ||
                NvHTTP::getXmlString(info, "PairStatus") != "1")
                throw std::runtime_error("Host is no longer paired or PyroWave-capable.");
            const auto hostMbps = NvHTTP::getXmlString(info, "PyroWaveHostLinkMbps").toLongLong();
            QHostAddress host(address.address());
            if (host.isNull()) {
                const auto resolved = QHostInfo::fromName(address.address());
                if (resolved.addresses().size() == 1) host = resolved.addresses().first();
            }
            const auto clientMbps = NetworkBuffers::routedWiredLinkMbps(host);
            const auto clientType = NetworkBuffers::routedLinkType(host);
            http.probePyroWaveDownloadKbps();
            std::array<std::int64_t, 3> samples{};
            for (auto& sample : samples) {
                if (QThread::currentThread()->isInterruptionRequested()) throw std::runtime_error("Calibration stopped.");
                sample = http.probePyroWaveDownloadKbps();
            }
            const auto recommended = PyroWaveBandwidth::ceilingKbps(samples, hostMbps, clientMbps);
            ceiling = int((std::min)(recommended, std::int64_t(2000000)));
            const auto known = [](qint64 speed) { return speed > 0 ? QString::number(speed) + " Mbps" : tr("unknown"); };
            message = tr("Measured host → client: %1 Mbps\nHost routed Ethernet: %2\nClient route (%5), physical Ethernet capacity: %3\nRecommended video ceiling: %4 Mbps (20% reserved).\nThis HTTPS measurement does not guarantee loss-free UDP streaming.")
                .arg((std::min)({samples[0], samples[1], samples[2]}) / 1000.0, 0, 'f', 1)
                .arg(known(hostMbps), known(clientMbps)).arg(ceiling / 1000.0, 0, 'f', 1).arg(clientType);
        } catch (const std::exception& e) { message = QString::fromUtf8(e.what()); }
        if (QThread::currentThread()->isInterruptionRequested()) { message = tr("Calibration stopped."); ceiling = 0; }
        QMetaObject::invokeMethod(this, [this, message, ceiling] { m_Message = message; m_Ceiling = ceiling; m_Running = false; emit changed(); }, Qt::QueuedConnection);
    });
    m_Worker->start();
}
