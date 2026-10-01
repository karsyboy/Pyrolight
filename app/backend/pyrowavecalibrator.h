#pragma once
#include <QObject>
#include <QVariantList>
#include <QThread>
class ComputerManager;
class PyroWaveCalibrator : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool running READ running NOTIFY changed)
    Q_PROPERTY(QString message READ message NOTIFY changed)
    Q_PROPERTY(int ceilingKbps READ ceilingKbps NOTIFY changed)
public:
    explicit PyroWaveCalibrator(QObject* parent = nullptr) : QObject(parent) {}
    ~PyroWaveCalibrator();
    bool running() const { return m_Running; }
    QString message() const { return m_Message; }
    int ceilingKbps() const { return m_Ceiling; }
    Q_INVOKABLE QVariantList hosts(ComputerManager* manager);
    Q_INVOKABLE void start(ComputerManager* manager, const QString& uuid);
    Q_INVOKABLE void cancel();
signals:
    void changed();
private:
    QThread* m_Worker = nullptr;
    bool m_Running = false;
    int m_Ceiling = 0;
    QString m_Message;
};
