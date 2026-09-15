#pragma once

#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <qqmlintegration.h>

// 对应旧版 backend/scanner.js：
//   递归扫描工作区 → 批量写入索引 → 逐个解析时长/分辨率/编码 → 首次扫描时按
//   文件夹名自动导入产品标签 → 递增 meta.version（通知其它端刷新）。
//
// 差别在于：旧版靠 Node 的异步 IO 天然不阻塞；C++ 这边为了扫描几千个文件时
// 界面还能动，采用「分批处理 + 让出事件循环」的方式推进，而不是一口气跑完。
class Scanner : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(Scanner)
    QML_SINGLETON

    Q_PROPERTY(bool scanning READ scanning NOTIFY stateChanged)
    Q_PROPERTY(QString phase READ phase NOTIFY stateChanged)
    Q_PROPERTY(int done READ done NOTIFY stateChanged)
    Q_PROPERTY(int total READ total NOTIFY stateChanged)
    Q_PROPERTY(int fileCount READ fileCount NOTIFY stateChanged)
    Q_PROPERTY(QString message READ message NOTIFY stateChanged)
    Q_PROPERTY(QString errorText READ errorText NOTIFY stateChanged)

public:
    explicit Scanner(QObject *parent = nullptr);

    bool scanning() const { return m_phase != Phase::Idle && m_phase != Phase::Done; }
    QString phase() const { return m_phaseText; }
    int done() const { return m_done; }
    int total() const { return m_total; }
    int fileCount() const { return m_fileCount; }
    QString message() const { return m_message; }
    QString errorText() const { return m_error; }

    // 启动一次完整扫描（工作区取自 Db 当前打开的根目录）
    Q_INVOKABLE void start();
    Q_INVOKABLE void cancel();

signals:
    void stateChanged();
    void finished(int fileCount);
    void failed(const QString &message);

private:
    enum class Phase { Idle, Walking, Indexing, Metadata, AutoTags, Done };

    void scheduleNext();
    void step();
    void walkStep();
    void indexStep();
    void metadataStep();
    void autoProductTags();
    void finish();

    Phase m_phase = Phase::Idle;
    QString m_phaseText;
    QString m_error;
    QString m_message;
    QString m_root;

    QStringList m_pendingDirs; // 待遍历目录
    QStringList m_files;       // 收集到的视频文件
    QSet<QString> m_visited;   // 真实路径去重（防符号链接绕圈）

    int m_indexPos = 0;
    int m_metaLastId = 0; // 元数据阶段用 id 游标推进，解析失败的文件不会卡住循环
    int m_done = 0;
    int m_total = 0;
    int m_fileCount = 0;
    bool m_firstScan = false;
    QTimer m_timer;
};
