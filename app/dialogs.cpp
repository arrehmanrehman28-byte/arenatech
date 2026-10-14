#include "dialogs.h"
#include "i18n.h"
#include "settings.h"
#include "kilogateway.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDialogButtonBox>
#include <QDesktopServices>
#include <QUrl>
#include <QClipboard>
#include <QGuiApplication>
#include <QIcon>
#include <QTimer>

namespace arena {

// --- NewProjectDialog ---------------------------------------------------------

NewProjectDialog::NewProjectDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle(arenaTr("dlg.newProject.title"));
    setMinimumWidth(460);
    auto *lay = new QVBoxLayout(this);

    m_name = new QLineEdit(QStringLiteral("Untitled"));
    m_resolution = new QComboBox;
    m_resolution->addItems({QStringLiteral("1920 × 1080 (1080p)"),
                            QStringLiteral("3840 × 2160 (4K)"),
                            QStringLiteral("1280 × 720 (720p)"),
                            QStringLiteral("1080 × 1920 (9:16 Reels)"),
                            QStringLiteral("1080 × 1080 (1:1)"),
                            QStringLiteral("1080 × 1350 (4:5)")});
    m_fps = new QComboBox;
    m_fps->addItems({QStringLiteral("24"), QStringLiteral("25"), QStringLiteral("30"),
                     QStringLiteral("50"), QStringLiteral("60")});
    m_fps->setCurrentText(QStringLiteral("30"));
    m_aspect = new QComboBox;
    m_aspect->addItems({QStringLiteral("16:9"), QStringLiteral("9:16"),
                        QStringLiteral("1:1"), QStringLiteral("4:5")});
    m_sampleRate = new QComboBox;
    m_sampleRate->addItems({QStringLiteral("44100 Hz"), QStringLiteral("48000 Hz"),
                            QStringLiteral("96000 Hz")});
    m_sampleRate->setCurrentIndex(1);
    m_proxy = new QCheckBox(arenaTr("dlg.newProject.proxy"));
    m_proxy->setChecked(true);

    auto *form = new QFormLayout;
    form->addRow(arenaTr("dlg.newProject.name"), m_name);
    form->addRow(arenaTr("dlg.newProject.resolution"), m_resolution);
    form->addRow(arenaTr("dlg.newProject.fps"), m_fps);
    form->addRow(arenaTr("dlg.newProject.aspect"), m_aspect);
    form->addRow(arenaTr("dlg.newProject.sampleRate"), m_sampleRate);
    form->addRow(m_proxy);
    lay->addLayout(form);

    m_preview = new QLabel;
    m_preview->setFrameShape(QFrame::StyledPanel);
    m_preview->setAlignment(Qt::AlignCenter);
    m_preview->setMinimumHeight(72);
    lay->addWidget(m_preview);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(arenaTr("dlg.newProject.create"));
    buttons->button(QDialogButtonBox::Ok)->setProperty("accent", QStringLiteral("true"));
    buttons->button(QDialogButtonBox::Cancel)->setText(arenaTr("dlg.cancel"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    lay->addWidget(buttons);

    const auto refresh = [this] { updatePreview(); };
    connect(m_resolution, &QComboBox::currentTextChanged, this, refresh);
    connect(m_fps, &QComboBox::currentTextChanged, this, refresh);
    connect(m_aspect, &QComboBox::currentTextChanged, this, refresh);
    updatePreview();
}

void NewProjectDialog::updatePreview()
{
    const QString res = m_resolution->currentText();
    const auto parts = res.split(QChar(0xD7)); // '×'
    int w = 1920, h = 1080;
    if (parts.size() == 2) {
        w = parts[0].trimmed().toInt();
        h = parts[1].section(QLatin1Char('('), 0, 0).trimmed().toInt();
    }
    m_preview->setText(arenaTr("dlg.newProject.preview",
                               {{QStringLiteral("width"), w}, {QStringLiteral("height"), h},
                                {QStringLiteral("fps"), m_fps->currentText()},
                                {QStringLiteral("aspect"), m_aspect->currentText()}}));
}

NewProjectResult NewProjectDialog::result() const
{
    NewProjectResult r;
    r.name = m_name->text();
    const auto parts = m_resolution->currentText().split(QChar(0xD7)); // '×'
    if (parts.size() == 2) {
        const int w = parts[0].trimmed().toInt();
        const int h = parts[1].section(QLatin1Char('('), 0, 0).trimmed().toInt();
        if (w > 0) r.width = w;
        if (h > 0) r.height = h;
    }
    const double fps = m_fps->currentText().toDouble();
    r.fps = fps > 0 ? fps : 30.0;
    r.aspect = m_aspect->currentText();
    const int rate = m_sampleRate->currentText().split(QLatin1Char(' ')).first().toInt();
    r.sampleRate = rate > 0 ? rate : 48000;
    r.proxy = m_proxy->isChecked();
    return r;
}

// --- AboutDialog --------------------------------------------------------------

AboutDialog::AboutDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle(arenaTr("dlg.about.title"));
    setMinimumWidth(420);
    setWindowIcon(QIcon(QStringLiteral(":/icon/app_icon.png")));
    auto *lay = new QVBoxLayout(this);
    // M9: app icon at the top of the About dialog (same asset as the window).
    auto *icon = new QLabel(this);
    icon->setPixmap(QIcon(QStringLiteral(":/icon/app_icon.png")).pixmap(96, 96));
    icon->setAlignment(Qt::AlignCenter);
    auto *name = new QLabel(QStringLiteral("Arena Edit"));
    QFont nf = name->font();
    nf.setPointSizeF(nf.pointSizeF() + 10);
    nf.setBold(true);
    name->setFont(nf);
    name->setAlignment(Qt::AlignCenter);
    auto *ver = new QLabel(arenaTr("dlg.about.version",
                                   {{QStringLiteral("version"), qApp->applicationVersion()},
                                    {QStringLiteral("date"), QStringLiteral(ARENA_BUILD_DATE)}}));
    ver->setProperty("role", QStringLiteral("muted"));
    ver->setAlignment(Qt::AlignCenter);
    auto *license = new QLabel(arenaTr("dlg.about.license"));
    license->setWordWrap(true);
    license->setProperty("role", QStringLiteral("muted"));
    license->setAlignment(Qt::AlignCenter);
    auto *credit = new QLabel(
        QStringLiteral("<a href=\"https://arenatechofficial.pages.dev\">%1</a>")
            .arg(arenaTr("dlg.about.credit")));
    credit->setProperty("role", QStringLiteral("ai"));
    credit->setAlignment(Qt::AlignCenter);
    credit->setOpenExternalLinks(true);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok);
    buttons->button(QDialogButtonBox::Ok)->setText(arenaTr("dlg.close"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    lay->addWidget(icon);
    lay->addWidget(name);
    lay->addWidget(ver);
    lay->addWidget(license);
    lay->addWidget(credit);
    lay->addWidget(buttons);
}

// --- WelcomeDialog ------------------------------------------------------------

WelcomeDialog::WelcomeDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle(arenaTr("welcome.title"));
    setMinimumWidth(520);
    auto *lay = new QVBoxLayout(this);
    auto *title = new QLabel(QStringLiteral("Arena Edit"));
    QFont tf = title->font();
    tf.setPointSizeF(tf.pointSizeF() + 14);
    tf.setBold(true);
    title->setFont(tf);
    title->setAlignment(Qt::AlignCenter);
    lay->addWidget(title);

    m_language = new QComboBox;
    m_language->addItem(arenaTr("settings.language.system"), QStringLiteral(""));
    for (const auto &loc : I18n::instance().locales())
        m_language->addItem(loc.nativeName, loc.bcp47);
    m_language->setCurrentIndex(m_language->findData(I18n::instance().currentLocale()));
    lay->addWidget(new QLabel(arenaTr("welcome.language")));
    lay->addWidget(m_language);

    auto *recentLabel = new QLabel(arenaTr("welcome.recent"));
    QFont rf = recentLabel->font();
    rf.setBold(true);
    recentLabel->setFont(rf);
    lay->addWidget(recentLabel);
    auto *recent = new QListWidget;
    recent->addItem(arenaTr("welcome.recent.empty"));
    recent->setMaximumHeight(120);
    lay->addWidget(recent);

    auto *card = new QFrame;
    card->setFrameShape(QFrame::StyledPanel);
    auto *cardLay = new QVBoxLayout(card);
    auto *whatsNew = new QLabel(arenaTr("welcome.whatsNew"));
    QFont wf = whatsNew->font();
    wf.setBold(true);
    whatsNew->setFont(wf);
    auto *body = new QLabel(arenaTr("welcome.whatsNew.body",
                                    {{QStringLiteral("version"), qApp->applicationVersion()}}));
    body->setProperty("role", QStringLiteral("muted"));
    body->setWordWrap(true);
    cardLay->addWidget(whatsNew);
    cardLay->addWidget(body);
    lay->addWidget(card);

    auto *buttons = new QHBoxLayout;
    auto *openBtn = new QPushButton(arenaTr("welcome.open"));
    auto *startBtn = new QPushButton(arenaTr("welcome.start"));
    startBtn->setProperty("accent", QStringLiteral("true"));
    buttons->addWidget(openBtn);
    buttons->addStretch(1);
    buttons->addWidget(startBtn);
    connect(startBtn, &QPushButton::clicked, this, &QDialog::accept);
    connect(openBtn, &QPushButton::clicked, this, &QDialog::reject);
    lay->addLayout(buttons);

    auto *credit = new QLabel(
        QStringLiteral("<a href=\"https://arenatechofficial.pages.dev\">%1</a>")
            .arg(arenaTr("dlg.about.credit")));
    credit->setProperty("role", QStringLiteral("ai"));
    credit->setAlignment(Qt::AlignCenter);
    credit->setOpenExternalLinks(true);
    lay->addWidget(credit);
}

QString WelcomeDialog::chosenLanguage() const
{
    return m_language->currentData().toString();
}

// --- SettingsDialog -----------------------------------------------------------

SettingsDialog::SettingsDialog(const QList<KiloModel> &models, QWidget *parent) : QDialog(parent)
{
    m_models = models;
    setWindowTitle(arenaTr("settings.title"));
    setMinimumSize(680, 440);
    auto *lay = new QHBoxLayout(this);
    lay->setContentsMargins(10, 10, 10, 10);

    m_search = new QLineEdit;
    m_search->setPlaceholderText(arenaTr("settings.search"));
    lay->addWidget(m_search, 2);

    auto *split = new QWidget;
    auto *splitLay = new QHBoxLayout(split);
    splitLay->setContentsMargins(0, 0, 0, 0);
    m_groups = new QListWidget;
    m_stack = new QStackedWidget;
    splitLay->addWidget(m_groups);
    splitLay->addWidget(m_stack, 1);
    lay->addWidget(split, 3);

    const QStringList groups = {
        QStringLiteral("settings.general"), QStringLiteral("settings.appearance"),
        QStringLiteral("settings.language"), QStringLiteral("settings.editing"),
        QStringLiteral("settings.playback"), QStringLiteral("settings.export"),
        QStringLiteral("settings.hardware"), QStringLiteral("settings.ai"),
        QStringLiteral("settings.privacy"), QStringLiteral("settings.keyboard")};
    for (const QString &g : groups) {
        m_groups->addItem(arenaTr(g));
        m_stack->addWidget(buildPage(g));
    }
    m_groups->setCurrentRow(0);
    connect(m_groups, &QListWidget::currentRowChanged, m_stack, &QStackedWidget::setCurrentIndex);
    connect(m_search, &QLineEdit::textChanged, this, &SettingsDialog::applySearch);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(arenaTr("dlg.ok"));
    buttons->button(QDialogButtonBox::Cancel)->setText(arenaTr("dlg.cancel"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    lay->addWidget(buttons);
    setMinimumWidth(700);
}

QWidget *SettingsDialog::buildPage(const QString &groupKey)
{
    auto *page = new QWidget;
    auto *lay = new QVBoxLayout(page);
    lay->setContentsMargins(16, 12, 16, 12);

    if (groupKey == QLatin1String("settings.general")) {
        auto *form = new QFormLayout;
        m_languageCombo = new QComboBox;
        m_languageCombo->addItem(arenaTr("settings.language.system"), QStringLiteral(""));
        for (const auto &loc : I18n::instance().locales())
            m_languageCombo->addItem(loc.nativeName, loc.bcp47);
        m_languageCombo->setCurrentIndex(m_languageCombo->findData(I18n::instance().currentLocale()));
        connect(m_languageCombo, &QComboBox::currentIndexChanged, this, [this](int) {
            emit languageChanged(m_languageCombo->currentData().toString());
        });
        form->addRow(arenaTr("settings.language"), m_languageCombo);
        lay->addLayout(form);
    } else if (groupKey == QLatin1String("settings.appearance")) {
        Settings &st = Settings::instance();
        auto *form = new QFormLayout;
        m_themeCombo = new QComboBox;
        const QStringList themeNames{QStringLiteral("dark"), QStringLiteral("light"),
                                     QStringLiteral("high-contrast")};
        for (const QString &t : themeNames)
            m_themeCombo->addItem(arenaTr(QStringLiteral("view.theme.") + t), t);
        m_themeCombo->setCurrentIndex(m_themeCombo->findData(st.theme()));
        connect(m_themeCombo, &QComboBox::currentIndexChanged, this, [this, &st](int) {
            st.setTheme(m_themeCombo->currentData().toString());
            emit themeChanged();
        });
        form->addRow(arenaTr("settings.theme"), m_themeCombo);

        m_fontScaleCombo = new QComboBox;
        m_fontScaleCombo->addItems({arenaTr("settings.fontScale.small"),
                                    arenaTr("settings.fontScale.default"),
                                    arenaTr("settings.fontScale.large"),
                                    arenaTr("settings.fontScale.xl")});
        m_fontScaleCombo->setCurrentIndex(st.fontScale());
        connect(m_fontScaleCombo, &QComboBox::currentIndexChanged, this, [this, &st](int idx) {
            st.setFontScale(idx);
            emit fontScaleChanged();
        });
        form->addRow(arenaTr("settings.fontScale"), m_fontScaleCombo);

        m_reduceMotion = new QCheckBox(arenaTr("settings.reduceMotion"));
        m_reduceMotion->setChecked(st.reduceMotion());
        connect(m_reduceMotion, &QCheckBox::toggled, &st, &Settings::setReduceMotion);
        form->addRow(m_reduceMotion);
        lay->addLayout(form);
    } else if (groupKey == QLatin1String("settings.language")) {
        auto *form = new QFormLayout;
        auto *note = new QLabel(arenaTr("settings.language.note"));
        note->setWordWrap(true);
        note->setProperty("role", QStringLiteral("muted"));
        form->addRow(note);
        lay->addLayout(form);
    } else if (groupKey == QLatin1String("settings.ai")) {
        Settings &st = Settings::instance();
        auto *form = new QFormLayout;
        m_aiEnable = new QCheckBox(arenaTr("settings.ai.enable"));
        m_aiEnable->setChecked(st.aiEnabled());
        connect(m_aiEnable, &QCheckBox::toggled, this, [this, &st](bool on) {
            st.setAiEnabled(on);
            emit aiToggled(on);
        });
        form->addRow(m_aiEnable);

        m_modelCombo = new QComboBox;
        m_modelCombo->addItem(QStringLiteral("auto"), QStringLiteral("auto"));
        for (const KiloModel &m : m_models)
            m_modelCombo->addItem(m.name + (m.isFree ? QStringLiteral("  (free)") : QString()), m.id);
        m_modelCombo->setCurrentIndex(m_modelCombo->findData(st.aiModel().isEmpty() ? QStringLiteral("auto") : st.aiModel()));
        connect(m_modelCombo, &QComboBox::currentIndexChanged, this, [this, &st](int) {
            st.setAiModel(m_modelCombo->currentData().toString());
            emit aiModelChanged(m_modelCombo->currentData().toString());
        });
        form->addRow(arenaTr("settings.ai.model"), m_modelCombo);

        m_budgetSpin = new QSpinBox;
        m_budgetSpin->setRange(1, 1000);
        m_budgetSpin->setValue(st.aiHourlyBudget());
        connect(m_budgetSpin, &QSpinBox::valueChanged, &st, &Settings::setAiHourlyBudget);
        connect(m_budgetSpin, &QSpinBox::valueChanged, this, [this](int n) {
            emit aiBudgetChanged(n);
        });
        form->addRow(arenaTr("settings.ai.budget"), m_budgetSpin);

        auto *hint = new QLabel(arenaTr("settings.ai.modelHint"));
        hint->setWordWrap(true);
        hint->setProperty("role", QStringLiteral("muted"));
        form->addRow(hint);
        lay->addLayout(form);
    } else if (groupKey == QLatin1String("settings.privacy")) {
        auto *form = new QFormLayout;
        auto *note = new QLabel(arenaTr("settings.privacy.note"));
        note->setWordWrap(true);
        auto *clearBtn = new QPushButton(arenaTr("settings.privacy.clearCache"));
        connect(clearBtn, &QPushButton::clicked, this, [this, clearBtn] {
            KiloClient::clearCache();
            clearBtn->setText(arenaTr("settings.privacy.cleared"));
            QTimer::singleShot(2000, this, [this, clearBtn] {
                clearBtn->setText(arenaTr("settings.privacy.clearCache"));
            });
        });
        form->addRow(note);
        form->addRow(clearBtn);
        lay->addLayout(form);
    } else {
        auto *later = new QLabel(arenaTr("settings.later"));
        later->setProperty("role", QStringLiteral("muted"));
        lay->addWidget(later);
    }
    lay->addStretch(1);
    return page;
}

// --- ErrorDialog ------------------------------------------------------------

ErrorDialog::ErrorDialog(const QString &message, const QString &details, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(arenaTr("dlg.error.title"));
    setMinimumWidth(460);
    auto *lay = new QVBoxLayout(this);
    auto *msg = new QLabel(message);
    msg->setWordWrap(true);
    m_details = new QPlainTextEdit(details);
    m_details->setReadOnly(true);
    m_details->setMaximumHeight(180);
    m_details->hide();
    m_toggle = new QPushButton(arenaTr("dlg.error.details"));
    m_toggle->setCheckable(true);
    connect(m_toggle, &QPushButton::toggled, m_details, &QWidget::setVisible);
    auto *buttons = new QHBoxLayout;
    auto *copy = new QPushButton(arenaTr("dlg.error.copy"));
    connect(copy, &QPushButton::clicked, this, [this, copy, details] {
        QGuiApplication::clipboard()->setText(details + QStringLiteral("\n") + m_details->toPlainText());
        copy->setText(arenaTr("dlg.copied"));
        QTimer::singleShot(2000, copy, [copy] { copy->setText(arenaTr("dlg.error.copy")); });
    });
    auto *close = new QPushButton(arenaTr("dlg.close"));
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    buttons->addWidget(copy);
    buttons->addStretch(1);
    buttons->addWidget(close);
    lay->addWidget(msg);
    lay->addWidget(m_details);
    lay->addWidget(m_toggle);
    lay->addLayout(buttons);
}

void SettingsDialog::applySearch()
{
    const QString q = m_search->text().trimmed().toLower();
    for (int i = 0; i < m_groups->count(); ++i)
        m_groups->item(i)->setHidden(!q.isEmpty()
                                        && !m_groups->item(i)->text().toLower().contains(q));
}

void SettingsDialog::retranslateUi()
{
    setWindowTitle(arenaTr("settings.title"));
    m_search->setPlaceholderText(arenaTr("settings.search"));
    const QStringList groups = {
        QStringLiteral("settings.general"), QStringLiteral("settings.appearance"),
        QStringLiteral("settings.language"), QStringLiteral("settings.editing"),
        QStringLiteral("settings.playback"), QStringLiteral("settings.export"),
        QStringLiteral("settings.hardware"), QStringLiteral("settings.ai"),
        QStringLiteral("settings.privacy"), QStringLiteral("settings.keyboard")};
    for (int i = 0; i < m_groups->count() && i < groups.size(); ++i)
        m_groups->item(i)->setText(arenaTr(groups.at(i)));
}

} // namespace arena
