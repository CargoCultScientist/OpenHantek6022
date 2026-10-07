// SPDX-License-Identifier: GPL-2.0-or-later
#include "capturedock.h"
#include "dsosettings.h"
#include "trendplot.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFrame>
#include <QHeaderView>
#include <QGridLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QTabWidget>
#include <QScrollArea>
#include <algorithm>
#include <cmath>
#include <functional>

namespace Lab {
static QString number(double value, Unit unit=UNIT_NONE) {
    return std::isfinite(value) ? valueToString(value,unit,5) : QString::fromUtf8("—");
}
static QColor channelColor(size_t channel) {
    static const std::array<QColor,3> colors={QColor("#fbd46a"),QColor("#68c8ee"),QColor("#d6a0ed")};
    return colors.at(channel);
}
// Use plain-text labels even for imported channel names. Colour reinforces the
// channel name, but is never the only indication of identity or validity.
class MeasurementCard : public QFrame {
public:
    QLabel *name, *state, *span;
    std::array<QLabel*,3> values{};
    QPushButton *details;
    MeasurementCard(size_t channel,QWidget *parent) : QFrame(parent) {
        setObjectName(QString("labMetricCard%1").arg(channel));
        setStyleSheet(QString("QFrame#%1 { background: #182638; border: 1px solid #304359; border-left: 3px solid %2; border-radius: 5px; }")
            .arg(objectName(),channelColor(channel).name()));
        auto grid=new QGridLayout(this); grid->setContentsMargins(10,6,10,6); grid->setVerticalSpacing(1);
        auto label=[&](const QString &id) {
            auto result=new QLabel(this); result->setTextFormat(Qt::PlainText);
            result->setObjectName(id+QString::number(channel)); return result;
        };
        name=label("labMetricName"); name->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);
        name->setStyleSheet(QString("color: %1; font-weight: 600;").arg(channelColor(channel).name()));
        grid->addWidget(name,0,0,1,2);
        details=new QPushButton(tr("Details"),this); details->setObjectName(QString("labMetricDetails%1").arg(channel));
        details->setAccessibleName(tr("Channel %1 measurement details").arg(channel+1));
        details->setStyleSheet("padding: 1px 8px; background: transparent;"); grid->addWidget(details,0,2,Qt::AlignRight);
        const QStringList titles={tr("Peak-to-peak"),tr("Frequency"),tr("RMS")};
        for(size_t i=0;i<values.size();++i) {
            auto caption=new QLabel(titles[int(i)],this); caption->setStyleSheet("color: #a6b9cf; font-size: 11px;");
            grid->addWidget(caption,1,int(i));
            values[i]=label(QString("labMetric%1Value").arg(i));
            values[i]->setStyleSheet("color: #edf4fb; font-size: 18px; font-weight: 600;");
            grid->addWidget(values[i],2,int(i)); grid->setColumnStretch(int(i),1);
        }
        state=label("labMetricStatus"); state->setWordWrap(true); grid->addWidget(state,3,0,1,3);
        span=label("labMetricSpan"); span->setStyleSheet("color: #a6b9cf; font-size: 11px;"); grid->addWidget(span,4,0,1,3);
    }
};
class CapturePlot : public QWidget {
public:
    using QWidget::QWidget;
    std::shared_ptr<const Capture> current, reference;
    bool align=false;
    double offset=0;
    double zoom=1, pan=0;
    TimeSpan gate;
    size_t maskChannel=0;
    double maskTolerance=unavailable;
    std::function<void()> viewChanged;
    std::function<void(double,bool)> cursorPlaced;
    QPoint dragStart;
    double dragPan=0;
    void fitRecord() {zoom=1;pan=0;update();if(viewChanged) viewChanged();}
    TimeSpan visibleSpan() const {
        double start=std::numeric_limits<double>::infinity(), end=-start;
        if(current) for(const auto &ch:current->channels) {
            if(ch.signal.samples.empty() || !(ch.signal.interval>0)) continue;
            const double origin=timeOrigin(*current,ch,align);
            start=std::min(start,origin);
            end=std::max(end,origin+(ch.signal.samples.size()-1)*ch.signal.interval);
        }
        if(!std::isfinite(start) || !std::isfinite(end)) return {0,1};
        if(end==start) {start-=.5e-6; end+=.5e-6;}
        const double span=end-start;
        start+=pan*span*(1-1/zoom);
        return {start,start+span/zoom};
    }
protected:
    void wheelEvent(QWheelEvent *event) override {
        const double previous=zoom;
        zoom=std::clamp(zoom*std::pow(1.2,event->angleDelta().y()/120.),1.0,1000.);
        const double cursor=std::clamp((event->position().x()-92)/std::max(1,width()-110),0.,1.);
        const double anchor=pan*(1-1/previous)+cursor/previous;
        pan=zoom>1?std::clamp((anchor-cursor/zoom)/(1-1/zoom),0.,1.):0;
        event->accept(); update(); if(viewChanged) viewChanged();
    }
    void mousePressEvent(QMouseEvent *event) override {
        dragStart=event->pos();dragPan=pan;
        if(event->button()==Qt::LeftButton && (event->modifiers() & (Qt::ShiftModifier|Qt::ControlModifier))) {
            const auto span=visibleSpan();
            const double position=std::clamp((event->position().x()-92)/std::max(1,width()-110),0.,1.);
            if(cursorPlaced) cursorPlaced(span.start+position*(span.end-span.start),event->modifiers().testFlag(Qt::ControlModifier));
        }
    }
    void mouseMoveEvent(QMouseEvent *event) override {
        if(event->buttons().testFlag(Qt::LeftButton) && event->modifiers()==Qt::NoModifier && zoom>1) {
            pan=std::clamp(dragPan-double(event->pos().x()-dragStart.x())/std::max(1,width()-110)/(zoom-1),0.,1.);
            update(); if(viewChanged) viewChanged();
        }
    }
    void mouseDoubleClickEvent(QMouseEvent *) override {fitRecord();}
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.fillRect(rect(),QColor("#101a27"));
        p.setPen(QColor("#b6c6d6"));
        if(!current) {p.drawText(rect(),Qt::AlignCenter,tr("Waiting for a displayed acquisition")); return;}
        const int count=int(std::count_if(current->channels.begin(),current->channels.end(),[](const auto &ch){return !ch.signal.samples.empty();}));
        const auto view=visibleSpan();
        const double start=view.start, end=view.end;
        int lane=0;
        for(int c=0;c<int(current->channels.size());++c) {
            const auto &ch=current->channels[size_t(c)];
            if(ch.signal.samples.empty()) continue;
            const CaptureChannel *ref=nullptr;
            if(reference && size_t(c)<reference->channels.size() && reference->channels[size_t(c)].unit==ch.unit &&
               (!align || (current->triggered && reference->triggered))) ref=&reference->channels[size_t(c)];
            auto bounds=std::minmax_element(ch.signal.samples.begin(),ch.signal.samples.end());
            double low=*bounds.first, high=*bounds.second;
            if(ref && !ref->signal.samples.empty()) {
                auto rb=std::minmax_element(ref->signal.samples.begin(),ref->signal.samples.end());
                low=std::min(low,*rb.first); high=std::max(high,*rb.second);
                if(size_t(c)==maskChannel && std::isfinite(maskTolerance)) {low-=maskTolerance; high+=maskTolerance;}
            }
            if(!std::isfinite(low)||!std::isfinite(high)||!(end>start)) continue;
            const double padding=std::max((high-low)*.12,1e-6);
            low-=padding; high+=padding;
            QRectF area(92, 22+lane++*(height()-30)/count, std::max(1,width()-110), std::max(1,(height()-30)/count-34));
            p.setPen(QColor("#26374a"));
            for(int i=0;i<=10;++i) p.drawLine(QPointF(area.left()+area.width()*i/10,area.top()),QPointF(area.left()+area.width()*i/10,area.bottom()));
            for(int i=0;i<=4;++i) p.drawLine(QPointF(area.left(),area.top()+area.height()*i/4),QPointF(area.right(),area.top()+area.height()*i/4));
            if(gate.end>=start && gate.start<=end) {
                auto x=[&](double time){return area.left()+std::clamp((time-start)/(end-start),0.,1.)*area.width();};
                p.fillRect(QRectF(QPointF(x(gate.start),area.top()),QPointF(x(gate.end),area.bottom())),QColor(135,183,228,30));
                p.setPen(QPen(QColor("#b9cee4"),1,Qt::DashLine));
                for(double time:{gate.start,gate.end}) if(std::isfinite(time) && time>=start && time<=end)
                    p.drawLine(QPointF(x(time),area.top()),QPointF(x(time),area.bottom()));
            }
            p.setPen(channelColor(size_t(c)));
            p.drawText(QRectF(4,area.top()-20,85,20),Qt::AlignLeft,ch.name.left(18));
            p.setPen(QColor("#b6c6d6"));
            p.drawText(QRectF(4,area.top(),85,20),Qt::AlignLeft,number(high,ch.unit));
            p.drawText(QRectF(4,area.bottom()-20,85,20),Qt::AlignLeft,number(low,ch.unit));
            p.drawText(QRectF(area.left(),area.bottom()+2,160,20),number(start,UNIT_SECONDS));
            p.drawText(QRectF(area.right()-160,area.bottom()+2,160,20),Qt::AlignRight,number(end,UNIT_SECONDS));
            auto draw=[&](const CaptureChannel &channel,double origin,bool dashed) {
                if(channel.signal.samples.empty() || !(channel.signal.interval>0)) return;
                p.save(); p.setClipRect(area);
                QPen pen(channelColor(size_t(c)),dashed?1.0:1.5,dashed?Qt::DashLine:Qt::SolidLine); p.setPen(pen);
                // Min/max envelope per pixel preserves narrow peaks when zoomed out.
                const int pixels=std::max(1,int(area.width()));
                QPointF previous; bool havePrevious=false;
                for(int x=0;x<pixels;++x) {
                    const double t0=start+(end-start)*x/pixels, t1=start+(end-start)*(x+1)/pixels;
                    const double rawBegin=(t0-origin)/channel.signal.interval;
                    const double rawEnd=(t1-origin)/channel.signal.interval;
                    if(rawEnd<0 || rawBegin>=double(channel.signal.samples.size())) {havePrevious=false; continue;}
                    size_t begin=size_t(std::max(0.0,std::floor(rawBegin)));
                    size_t finish=size_t(std::min(double(channel.signal.samples.size()),std::max(double(begin+1),std::ceil(rawEnd))));
                    const auto limits=std::minmax_element(channel.signal.samples.begin()+begin,channel.signal.samples.begin()+finish);
                    auto point=[&](double value) {return QPointF(area.left()+x,area.bottom()-(value-low)/(high-low)*area.height());};
                    if(dashed && size_t(c)==maskChannel && std::isfinite(maskTolerance)) {
                        const auto top=point(*limits.second+maskTolerance), bottom=point(*limits.first-maskTolerance);
                        p.fillRect(QRectF(top,QSizeF(1,std::max(1.,bottom.y()-top.y()))),QColor(87,218,170,55));
                    }
                    p.drawLine(point(*limits.first),point(*limits.second));
                    const auto first=point(channel.signal.samples[begin]);
                    if(havePrevious) p.drawLine(previous,first);
                    previous=point(channel.signal.samples[finish-1]); havePrevious=true;
                }
                p.restore();
            };
            if(ref) draw(*ref,timeOrigin(*reference,*ref,align)+offset,true);
            draw(ch,timeOrigin(*current,ch,align),false);
        }
    }
};

CaptureDock::CaptureDock(const DsoSettings *settings, QWidget *parent)
    : QDockWidget(tr("Capture history & measurements"),parent),settings(settings) {
    setObjectName("labCaptureHistory");
    setAllowedAreas(Qt::BottomDockWidgetArea|Qt::TopDockWidgetArea);
    auto body=new QWidget(this); auto layout=new QVBoxLayout(body);
    body->setObjectName("labWorkbench");
    body->setStyleSheet(QStringLiteral(R"(
        QWidget#labWorkbench { background: #101824; color: #dce7f3; }
        QWidget#labWorkbench QLabel, QWidget#labWorkbench QCheckBox { color: #c7d6e6; }
        QWidget#labWorkbench QPushButton, QWidget#labWorkbench QComboBox, QWidget#labWorkbench QDoubleSpinBox {
            background: #203044; color: #e4eef8; border: 1px solid #3b526b; border-radius: 5px; padding: 5px 8px;
        }
        QWidget#labWorkbench QPushButton:hover { background: #2e4560; border-color: #62c7d5; }
        QWidget#labWorkbench QPushButton:pressed { background: #355d70; }
        QWidget#labWorkbench QPushButton:checked { background: #284b60; border-color: #79dfda; }
        QWidget#labWorkbench QPushButton:disabled { color: #8092a6; background: #162333; border-color: #293c52; }
        QWidget#labWorkbench QMenu { background: #203044; color: #e4eef8; border: 1px solid #526b85; }
        QWidget#labWorkbench QMenu::item { padding: 8px 18px; }
        QWidget#labWorkbench QMenu::item:selected { background: #355d70; }
        QWidget#labWorkbench QPushButton:focus, QWidget#labWorkbench QComboBox:focus,
        QWidget#labWorkbench QDoubleSpinBox:focus { border-color: #79dfda; }
        QWidget#labWorkbench QDoubleSpinBox:disabled { color: #71839a; background: #162333; }
        QWidget#labWorkbench QTabWidget::pane { border: 1px solid #2d4056; border-radius: 6px; }
        QWidget#labWorkbench QTabBar::tab { background: #172536; color: #9eb3c9; padding: 7px 18px; }
        QWidget#labWorkbench QTabBar::tab:selected { background: #284054; color: #a4f1e4; border-bottom: 2px solid #76d9cf; }
        QWidget#labWorkbench QTableWidget, QWidget#labWorkbench QListWidget {
            background: #152132; alternate-background-color: #1a2a3d; color: #dce7f3;
            border: 1px solid #2d4056; border-radius: 5px; gridline-color: #293c52;
            selection-background-color: #2b5771; selection-color: #ffffff;
        }
        QWidget#labWorkbench QHeaderView::section { background: #23354a; color: #c7d8e9; padding: 6px; border: none; }
        QWidget#labWorkbench QHeaderView { background: #23354a; }
        QWidget#labWorkbench QTableCornerButton::section { background: #23354a; border: none; }
        QWidget#labWorkbench QListWidget::item { padding: 7px; border-bottom: 1px solid #233449; }
        QWidget#labWorkbench QSplitter::handle { background: #263b4f; }
        QWidget#labWorkbench QScrollBar:vertical { background: #142334; width: 12px; margin: 0; }
        QWidget#labWorkbench QScrollBar:horizontal { background: #142334; height: 12px; margin: 0; }
        QWidget#labWorkbench QScrollBar::handle { background: #426079; border-radius: 4px; min-width: 24px; min-height: 24px; }
        QWidget#labWorkbench QScrollBar::handle:hover { background: #6395b3; }
        QWidget#labWorkbench QScrollBar::add-line, QWidget#labWorkbench QScrollBar::sub-line { width: 0; height: 0; }
        QWidget#labWorkbench QScrollBar::add-page, QWidget#labWorkbench QScrollBar::sub-page { background: #142334; }
        QWidget#labWorkbench QComboBox QAbstractItemView { background: #203044; color: #e4eef8; selection-background-color: #355d70; }
    )"));
    layout->setContentsMargins(12,10,12,10); layout->setSpacing(8);
    auto controls=new QHBoxLayout;
    auto title=new QLabel(tr("Capture Lab"),body); title->setStyleSheet("color: #dce7f3; font-size: 16px; font-weight: 600; padding-right: 12px;");
    controls->addWidget(title);
    auto button=[&](const QString &text,auto callback) {
        auto b=new QPushButton(text,body); controls->addWidget(b); connect(b,&QPushButton::clicked,this,callback); return b;
    };
    saveButton=button(tr("Save capture…"),[this]{saveCapture();}); saveButton->setObjectName("labSaveCapture");
    button(tr("Open capture…"),[this]{openCapture();});
    referenceButton=button(tr("Set reference"),[this]{reference=selected; resetMask();});
    referenceButton->setObjectName("labSetReference");
    controls->addStretch();
    auto configure=new QPushButton(tr("Analysis settings"),body); configure->setObjectName("labConfigure"); configure->setCheckable(true);
    configure->setToolTip(tr("Show measurement, reference, mask and session-log settings. This does not change acquisition settings.")); controls->addWidget(configure);
    auto focus=new QPushButton(tr("Focus view"),body); focus->setObjectName("labFocusView"); focus->setCheckable(true);
    focus->setToolTip(tr("Hide history and analysis settings to give the current view more space. Recording and logging are unchanged.")); controls->addWidget(focus);
    auto more=new QPushButton(tr("More"),body); more->setObjectName("labMore");
    auto menu=new QMenu(more); auto clearAction=menu->addAction(tr("Clear history…"),this,&CaptureDock::clearHistory);
    clearAction->setObjectName("labClearHistory"); more->setMenu(menu); controls->addWidget(more); layout->addLayout(controls);
    auto activity=new QHBoxLayout;
    record=new QCheckBox(tr("Record history"),body); record->setObjectName("labRecordHistory"); record->setChecked(true);
    record->setToolTip(tr("Retain displayed captures in RAM. Turning this off does not stop acquisition or session logging."));
    live=new QCheckBox(tr("Follow latest"),body); live->setObjectName("labFollowLatest"); live->setChecked(true);
    live->setToolTip(tr("Follow newly retained captures. This is a browser setting, not the scope's Run/Stop control."));
    activity->addWidget(record); activity->addWidget(live);
    selectionBadge=new QLabel(body); selectionBadge->setObjectName("labSelectionBadge");
    selectionBadge->setToolTip(tr("Capture-browser state only. Use the oscilloscope's acquisition controls for Run/Stop or SINGLE."));
    activity->addWidget(selectionBadge); activity->addStretch();
    logBadge=new QLabel(body); logBadge->setObjectName("labLogBadge"); logBadge->setTextFormat(Qt::PlainText);
    activity->addWidget(logBadge); layout->addLayout(activity);
    auto tabs=new QTabWidget(body); tabs->setObjectName("labControls");
    auto page=[&](const QString &name) {
        auto widget=new QWidget(tabs); auto box=new QVBoxLayout(widget); box->setContentsMargins(10,6,10,6);
        tabs->addTab(widget,name); return box;
    };
    auto measurePage=page(tr("Measure")); auto referencePage=page(tr("Reference")); auto maskPage=page(tr("Mask test")); auto logPage=page(tr("Session log"));
    layout->addWidget(tabs);
    tabs->hide();
    connect(configure,&QPushButton::toggled,tabs,&QWidget::setVisible);
    auto comparison=new QHBoxLayout;
    align=new QCheckBox(tr("Align trigger"),body);
    align->setToolTip(tr("Time zero is the trigger when available, otherwise the record start. Reference comparison requires both captures to be triggered."));
    comparison->addWidget(align);
    comparison->addWidget(new QLabel(tr("Reference time shift (s):"),body));
    offset=new QDoubleSpinBox(body); offset->setDecimals(9); offset->setRange(-1e3,1e3); offset->setSingleStep(.000001);
    comparison->addWidget(offset);
    clearReferenceButton=new QPushButton(tr("Clear reference"),body); comparison->addWidget(clearReferenceButton);
    connect(clearReferenceButton,&QPushButton::clicked,this,[this]{reference.reset(); resetMask();});
    comparison->addStretch(); referencePage->addLayout(comparison);
    referencePage->addWidget(new QLabel(tr("Solid trace: selected capture   /   Dashed trace: pinned reference   /   Differences use interpolation, never extrapolation."),body));
    auto measurementControls=new QHBoxLayout;
    measurementControls->addWidget(new QLabel(tr("Measure:"),body));
    spanMode=new QComboBox(body); spanMode->setObjectName("labMeasurementSpan");
    spanMode->addItems({tr("Whole record"),tr("Visible window"),tr("Between cursors")});
    measurementControls->addWidget(spanMode);
    auto cursor=[&](const QString &label,const QString &name) {
        measurementControls->addWidget(new QLabel(label,body));
        auto box=new QDoubleSpinBox(body); box->setObjectName(name); box->setDecimals(12);
        box->setRange(-1e6,1e6); box->setSingleStep(.000001); box->setSuffix(" s");
        box->setKeyboardTracking(false); box->setEnabled(false); measurementControls->addWidget(box); return box;
    };
    cursorA=cursor(tr("A:"),"labCursorA"); cursorB=cursor(tr("B:"),"labCursorB"); cursorB->setValue(.001);
    measurementControls->addWidget(new QLabel(tr("Shift-click: A · Ctrl-click: B"),body));
    details=new QCheckBox(tr("More measurements"),body); measurementControls->addWidget(details);
    measurementControls->addStretch(); measurePage->addLayout(measurementControls);
    auto statisticsControls=new QHBoxLayout;
    statisticsControls->addWidget(new QLabel(tr("Live statistics:"),body));
    statisticMetric=new QComboBox(body); statisticMetric->setObjectName("labStatisticMetric");
    statisticMetric->addItems({tr("Vpp"),tr("RMS"),tr("Frequency")}); statisticsControls->addWidget(statisticMetric);
    auto reset=new QPushButton(tr("Reset statistics"),body); reset->setObjectName("labResetStatistics");
    statisticsControls->addWidget(reset);
    statisticsControls->addWidget(new QLabel(tr("New recorded acquisitions only · Reset on span or setup change"),body));
    statisticsControls->addStretch(); measurePage->addLayout(statisticsControls);
    auto maskControls=new QHBoxLayout;
    maskEnabled=new QCheckBox(tr("Enable whole-record mask"),body); maskEnabled->setObjectName("labMaskEnabled"); maskControls->addWidget(maskEnabled);
    maskChannel=new QComboBox(body); maskChannel->setObjectName("labMaskChannel"); maskChannel->addItems({tr("CH1"),tr("CH2"),tr("MATH")}); maskControls->addWidget(maskChannel);
    maskControls->addWidget(new QLabel(tr("Tolerance ±"),body));
    maskTolerance=new QDoubleSpinBox(body); maskTolerance->setObjectName("labMaskTolerance");
    maskTolerance->setDecimals(3); maskTolerance->setRange(0,1e6); maskTolerance->setValue(5); maskTolerance->setKeyboardTracking(false); maskControls->addWidget(maskTolerance);
    toleranceMode=new QComboBox(body); toleranceMode->setObjectName("labMaskToleranceMode");
    toleranceMode->addItems({tr("% of reference Vpp"),tr("channel units (V / V² / W / 1)")}); maskControls->addWidget(toleranceMode);
    freezeOnFailure=new QCheckBox(tr("Freeze view on failure"),body); freezeOnFailure->setObjectName("labFreezeOnFailure"); maskControls->addWidget(freezeOnFailure);
    auto resetMaskButton=new QPushButton(tr("Reset counters"),body); maskControls->addWidget(resetMaskButton);
    maskControls->addStretch(); maskPage->addLayout(maskControls);
    maskSummary=new QLabel(body); maskSummary->setObjectName("labMaskSummary"); maskSummary->setWordWrap(true); maskPage->addWidget(maskSummary);
    connect(resetMaskButton,&QPushButton::clicked,this,&CaptureDock::resetMask);
    connect(maskEnabled,&QCheckBox::toggled,this,&CaptureDock::resetMask);
    connect(maskChannel,qOverload<int>(&QComboBox::currentIndexChanged),this,&CaptureDock::resetMask);
    connect(toleranceMode,qOverload<int>(&QComboBox::currentIndexChanged),this,[this](int mode){maskTolerance->setDecimals(mode?9:3); resetMask();});
    connect(maskTolerance,qOverload<double>(&QDoubleSpinBox::valueChanged),this,&CaptureDock::resetMask);
    auto logControls=new QHBoxLayout;
    collectLog=new QCheckBox(tr("Collect measurements (RAM)"),body); collectLog->setObjectName("labCollectLog"); logControls->addWidget(collectLog);
    logChannel=new QComboBox(body); logChannel->setObjectName("labLogChannel"); logChannel->addItems({tr("CH1"),tr("CH2"),tr("MATH")}); logControls->addWidget(logChannel);
    logControls->addWidget(new QLabel(tr("Minimum interval:"),body));
    logInterval=new QDoubleSpinBox(body); logInterval->setObjectName("labLogInterval"); logInterval->setRange(0,3600); logInterval->setDecimals(3);
    logInterval->setSuffix(" s"); logInterval->setSpecialValueText(tr("Every displayed capture")); logInterval->setSingleStep(1); logControls->addWidget(logInterval);
    logControls->addWidget(new QLabel(tr("Trend:"),body));
    trendMetric=new QComboBox(body); trendMetric->setObjectName("labTrendMetric"); trendMetric->addItems({tr("Vpp"),tr("RMS"),tr("Frequency"),tr("Mean")}); logControls->addWidget(trendMetric);
    auto exportButton=new QPushButton(tr("Export log CSV…"),body); exportButton->setObjectName("labExportLog"); logControls->addWidget(exportButton);
    auto clearLogButton=new QPushButton(tr("Clear log"),body); clearLogButton->setObjectName("labClearLog"); logControls->addWidget(clearLogButton);
    logControls->addStretch(); logPage->addLayout(logControls);
    logSummary=new QLabel(body); logSummary->setObjectName("labLogSummary"); logSummary->setWordWrap(true); logPage->addWidget(logSummary);
    connect(exportButton,&QPushButton::clicked,this,&CaptureDock::exportLog);
    connect(clearLogButton,&QPushButton::clicked,this,[this]{
        const bool collecting=collectLog->isChecked();
        if(!confirmDiscardLog()) return;
        measurementLog.clear(); logRevision=exportedLogRevision=0;
        collectLog->setChecked(collecting); refresh();
    });
    connect(collectLog,&QCheckBox::toggled,this,[this](bool enabled){if(enabled) measurementLog.breakSegment(); refresh();});
    auto splitter=new QSplitter(body);
    auto sidebar=new QWidget(splitter); sidebar->setObjectName("labHistorySidebar"); sidebar->setMinimumWidth(175); sidebar->setMaximumWidth(240);
    auto sidebarLayout=new QVBoxLayout(sidebar); sidebarLayout->setContentsMargins(0,0,0,0);
    historyFilter=new QComboBox(sidebar); historyFilter->setObjectName("labHistoryFilter");
    historyFilter->setToolTip(tr("Filter the list under the current mask criterion. The selected waveform is unchanged until you select another capture."));
    historyFilter->addItems({tr("All captures"),tr("Mask failures"),tr("Mask passes"),tr("Not testable")}); sidebarLayout->addWidget(historyFilter);
    list=new QListWidget(sidebar); list->setObjectName("labCaptureList"); sidebarLayout->addWidget(list);
    connect(focus,&QPushButton::toggled,this,[sidebar,tabs,configure](bool focused){
        sidebar->setVisible(!focused); tabs->setVisible(!focused && configure->isChecked()); configure->setEnabled(!focused);
    });
    auto views=new QTabWidget(splitter); views->setObjectName("labViews");
    plot=new CapturePlot(views); plot->setObjectName("labCapturePlot"); plot->setMinimumSize(420,180);
    trend=new TrendPlot(&measurementLog,views); trend->setObjectName("labTrendPlot");
    views->addTab(plot,tr("Waveform")); views->addTab(trend,tr("Trend")); splitter->setStretchFactor(1,1);
    plot->setToolTip(tr("Wheel to zoom · Drag to pan · Double-click to reset · Shift-click cursor A · Ctrl-click cursor B"));
    auto fit=new QPushButton(tr("Fit record"),views); fit->setObjectName("labFitRecord");
    fit->setToolTip(tr("Reset waveform zoom and pan. Acquisition settings are unchanged.")); views->setCornerWidget(fit);
    connect(fit,&QPushButton::clicked,this,[this]{plot->fitRecord();});
    connect(views,&QTabWidget::currentChanged,this,[fit](int index){fit->setVisible(index==0);});
    connect(trendMetric,qOverload<int>(&QComboBox::currentIndexChanged),this,[this](int metric){trend->metric=metric; trend->update();});
    plot->viewChanged=[this]{if(spanMode->currentIndex()==1) resetStatistics(); else refresh();};
    plot->cursorPlaced=[this](double value,bool second){
        (second?cursorB:cursorA)->setValue(value); spanMode->setCurrentIndex(2);
    };
    layout->addWidget(splitter,1);
    auto meterRow=new QHBoxLayout;
    for(size_t channel=0;channel<quickMetrics.size();++channel) {
        auto meter=new MeasurementCard(channel,body); meterRow->addWidget(meter,1); quickMetrics[channel]=meter;
        connect(meter->details,&QPushButton::clicked,this,[this,views,channel]{
            views->setCurrentWidget(measurements); measurements->selectRow(int(channel));
        });
    }
    layout->addLayout(meterRow);
    measurements=new QTableWidget(views); views->addTab(measurements,tr("Measurements"));
    measurements->setObjectName("labMeasurements"); measurements->setColumnCount(22);
    measurements->setHorizontalHeaderLabels({tr("Channel / span"),tr("Vpp"),tr("Mean"),tr("RMS"),tr("Frequency"),tr("Duty +"),
        tr("Width +"),tr("Rise 10–90"),tr("Fall 90–10"),tr("Δ RMS"),tr("Δ max"),tr("Status"),tr("Live Vpp statistics"),
        tr("Minimum"),tr("Maximum"),tr("AC RMS"),tr("Period"),tr("Duty −"),tr("Width −"),tr("Cycle mean"),tr("Cycle RMS"),tr("Crest factor")});
    for(int c=13;c<measurements->columnCount();++c) measurements->setColumnHidden(c,true);
    measurements->setEditTriggers(QAbstractItemView::NoEditTriggers);
    measurements->setAlternatingRowColors(true);
    measurements->setSelectionBehavior(QAbstractItemView::SelectRows);
    measurements->verticalHeader()->hide();
    measurements->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    measurements->setMinimumHeight(160);
    status=new QLabel(body); status->setWordWrap(true); layout->addWidget(status);
    auto scroll=new QScrollArea(this); scroll->setObjectName("labWorkbenchScroll");
    scroll->setWidgetResizable(true); scroll->setFrameShape(QFrame::NoFrame); scroll->setWidget(body); scroll->setMinimumHeight(300);
    scroll->setStyleSheet("QScrollArea { background: #101824; } QScrollBar {background: #152132;} QScrollBar::handle {background: #426079;}");
    setWidget(scroll);
    connect(list,&QListWidget::currentRowChanged,this,[this](int row) {
        if(row<0 || size_t(row)>=history.frames().size()) return;
        live->setChecked(false); selected=history.frames()[size_t(row)]; refresh();
    });
    connect(live,&QCheckBox::toggled,this,[this](bool enabled){
        if(enabled && !history.frames().empty()) {selected=history.frames().back(); refreshList();}
        refresh();
    });
    connect(record,&QCheckBox::toggled,this,[this]{refresh();});
    connect(align,&QCheckBox::toggled,this,[this]{statistics.clear(); resetMask();});
    connect(offset,qOverload<double>(&QDoubleSpinBox::valueChanged),this,&CaptureDock::resetMask);
    connect(historyFilter,qOverload<int>(&QComboBox::currentIndexChanged),this,[this]{refreshList(); refresh();});
    connect(spanMode,qOverload<int>(&QComboBox::currentIndexChanged),this,[this](int mode){
        cursorA->setEnabled(mode==2); cursorB->setEnabled(mode==2); resetStatistics();
    });
    for(auto box:{cursorA,cursorB}) connect(box,qOverload<double>(&QDoubleSpinBox::valueChanged),this,[this]{
        if(spanMode->currentIndex()==2) resetStatistics();
    });
    connect(reset,&QPushButton::clicked,this,&CaptureDock::resetStatistics);
    connect(statisticMetric,qOverload<int>(&QComboBox::currentIndexChanged),this,[this]{refresh();});
    connect(details,&QCheckBox::toggled,this,[this,views](bool enabled){
        for(int c=13;c<measurements->columnCount();++c) measurements->setColumnHidden(c,!enabled);
        if(enabled) views->setCurrentWidget(measurements);
    });
    connect(this,&QDockWidget::visibilityChanged,this,[this](bool visible){if(visible) refresh();});
    refresh();
}
void CaptureDock::ingest(const std::shared_ptr<PPresult> &frame) {
    if(!frame) return;
    // Do not copy repeated redraws of a stopped/held acquisition.
    if(received && lastReceivedTag==frame->tag && lastReceivedTime==frame->capturedAtMs) return;
    received=true; lastReceivedTag=frame->tag; lastReceivedTime=frame->capturedAtMs;
    if(!record->isChecked() && !collectLog->isChecked()) return;
    auto capture=Capture::fromResult(*frame,*settings);
    if(!capture) return;
    const bool retained=record->isChecked() && history.append(capture);
    if(retained && live->isChecked()) selected=capture;
    if(retained && maskEnabled->isChecked()) {
        const auto result=maskResult(*capture);
        maskResults[capture]=result;
        if(result.state==MaskState::Pass) ++maskPassed;
        else if(result.state==MaskState::Fail) {
            ++maskFailed;
            if(freezeOnFailure->isChecked() && live->isChecked()) {selected=capture; live->setChecked(false);}
        } else ++maskUntestable;
    }
    plot->current=selected; plot->align=align->isChecked();
    if(retained) statistics.add(*capture,measurementSpan(),align->isChecked());
    if(collectLog->isChecked() && measurementLog.append(*capture,{size_t(logChannel->currentIndex()),measurementSpan(),align->isChecked(),qRound64(logInterval->value()*1000)})) ++logRevision;
    if(retained) refreshList();
    trend->update();
    if(isVisible()) refresh();
}
TimeSpan CaptureDock::measurementSpan() const {
    if(spanMode->currentIndex()==1) return selected?plot->visibleSpan():TimeSpan{1,0};
    if(spanMode->currentIndex()==2) return {std::min(cursorA->value(),cursorB->value()),std::max(cursorA->value(),cursorB->value())};
    return {};
}
void CaptureDock::resetStatistics() {statistics.clear(); refresh();}
MaskSpec CaptureDock::maskSpec() const {
    return {size_t(maskChannel->currentIndex()),toleranceMode->currentIndex()==1?maskTolerance->value():0,
        toleranceMode->currentIndex()==0?maskTolerance->value():0};
}
MaskResult CaptureDock::maskResult(const Capture &capture) const {return testMask(capture,reference.get(),maskSpec(),align->isChecked(),offset->value());}
void CaptureDock::resetMask() {maskPassed=maskFailed=maskUntestable=0; maskResults.clear(); refreshList(); refresh();}
void CaptureDock::refreshList() {
    QSignalBlocker blocker(list); list->clear();
    std::map<std::shared_ptr<const Capture>,MaskResult> retained;
    int index=0;
    for(const auto &frame:history.frames()) {
        auto item=new QListWidgetItem(QString("#%1  ·  %2").arg(frame->tag).arg(QDateTime::fromMSecsSinceEpoch(frame->capturedAtMs).toString("HH:mm:ss.zzz")),list);
        if(maskEnabled->isChecked()) {
            const auto found=maskResults.find(frame);
            const auto result=found==maskResults.end()?maskResult(*frame):found->second;
            retained.emplace(frame,result);
            item->setText(item->text()+"\n"+maskStateName(result.state));
            item->setToolTip(result.reason.isEmpty()?tr("%1 / %2 samples outside tolerance").arg(result.outside).arg(result.tested):result.reason);
            item->setForeground(result.state==MaskState::Fail?QColor("#ff9d9d"):result.state==MaskState::Pass?QColor("#99e9c2"):QColor("#c5cedb"));
            const int filter=historyFilter->currentIndex();
            item->setHidden((filter==1 && result.state!=MaskState::Fail)||(filter==2 && result.state!=MaskState::Pass)||(filter==3 && result.state!=MaskState::Untestable));
        } else item->setHidden(historyFilter->currentIndex()!=0);
        if(frame==selected) list->setCurrentRow(index);
        ++index;
    }
    maskResults=std::move(retained); // Evicted captures must not remain retained by the result cache.
    if(live->isChecked()) list->scrollToBottom();
}
void CaptureDock::refresh() {
    plot->current=selected; plot->reference=reference; plot->align=align->isChecked(); plot->offset=offset->value();
    const auto lanes=selected?std::count_if(selected->channels.begin(),selected->channels.end(),[](const auto &ch){return !ch.signal.samples.empty();}):0;
    plot->setMinimumHeight(std::max(180,30+int(lanes)*74));
    const auto span=measurementSpan(); plot->gate=span; plot->update();
    const auto found=maskResults.find(selected);
    const auto mask=selected && maskEnabled->isChecked()?(found!=maskResults.end()?found->second:maskResult(*selected)):MaskResult{};
    plot->maskChannel=size_t(maskChannel->currentIndex());
    plot->maskTolerance=maskEnabled->isChecked() && mask.state!=MaskState::Untestable?mask.tolerance:unavailable;
    selectionBadge->setText(selected?(!record->isChecked()?tr("HISTORY PAUSED  ·  #%1"):live->isChecked()?tr("FOLLOWING LATEST  ·  #%1"):tr("VIEW FROZEN  ·  #%1")).arg(selected->tag):tr("NO CAPTURE"));
    if(selected && maskEnabled->isChecked()) selectionBadge->setText(selectionBadge->text()+"  ·  "+maskStateName(mask.state));
    if(list->currentItem() && list->currentItem()->isHidden()) selectionBadge->setText(selectionBadge->text()+tr("  ·  outside filter"));
    const QString badgeColor=maskEnabled->isChecked()?(mask.state==MaskState::Fail?"#ffb0b0":mask.state==MaskState::Pass?"#a5efd0":"#ecd09d"):"#bcf0e5";
    selectionBadge->setStyleSheet(QString("background: #243d50; color: %1; border-radius: 5px; padding: 5px 10px;").arg(badgeColor));
    saveButton->setEnabled(bool(selected)); referenceButton->setEnabled(bool(selected)); clearReferenceButton->setEnabled(bool(reference));
    const bool unsaved=logRevision!=exportedLogRevision;
    logBadge->setText(tr("Log: %1 · %2 in RAM%3")
        .arg(collectLog->isChecked()?tr("collecting"):tr("paused")).arg(measurementLog.entries().size())
        .arg(unsaved?tr(" · unexported"):QString()));
    logBadge->setStyleSheet(QString("color: %1;").arg(unsaved?"#ecd09d":"#a6b9cf"));
    logBadge->setToolTip(tr("Session-log collection is independent of history and Follow latest. RAM readings are lost on a crash; export CSV to keep them."));
    Unit maskUnit=UNIT_NONE;
    if(reference && maskSpec().channel<reference->channels.size()) maskUnit=reference->channels[maskSpec().channel].unit;
    maskSummary->setText(maskEnabled->isChecked()?
        tr("Selected: %1  ·  %2   |   New captures: %3 pass / %4 fail / %5 not testable. Full reference coverage required.")
            .arg(selected?maskStateName(mask.state):tr("none"),mask.reason.isEmpty()?tr("%1 outside / %2 samples · ±%3").arg(mask.outside).arg(mask.tested).arg(number(mask.tolerance,maskUnit)):mask.reason)
            .arg(maskPassed).arg(maskFailed).arg(maskUntestable):
        tr("Pin a reference, choose a tolerance, then enable testing. Green band = reference ± tolerance. No acquisition settings are changed."));
    logSummary->setText(tr("%1  ·  %2 / 10,000 readings in RAM  ·  %3 evicted. Export to keep them. Uses the selected measurement span; setup changes start a new trend segment.")
        .arg(collectLog->isChecked()?tr("COLLECTING"):tr("PAUSED")).arg(measurementLog.entries().size()).arg(measurementLog.evicted()));
    trend->update();
    measurements->horizontalHeaderItem(12)->setText(tr("Live %1 statistics").arg(statisticMetric->currentText()));
    measurements->setRowCount(selected?int(selected->channels.size()):0);
    for(auto meter:quickMetrics) meter->hide();
    if(selected) for(size_t c=0;c<selected->channels.size();++c) {
        const auto &ch=selected->channels[c];
        const double origin=timeOrigin(*selected,ch,align->isChecked());
        const auto range=sampleRange(ch.signal.samples.size(),ch.signal.interval,origin,span);
        const auto m=measure(ch.signal.samples,ch.signal.interval,ch.valid,range);
        const auto difference=reference?compare(*selected,*reference,c,align->isChecked(),offset->value(),span):Difference{};
        QString state;
        if(!m.count) state=tr("No finite samples in span");
        else if(m.clipped) state=tr("CLIPPED: timing unavailable");
        else if(m.irregular) state=tr("Irregular periods");
        else if(m.undersampled) state=tr("Sampling-limited timing");
        else if(!std::isfinite(m.frequency)) state=tr("Insufficient complete cycles");
        else state=tr("Interpolated timing estimate");
        if(!ch.signal.samples.empty()) {
            auto meter=quickMetrics[c]; meter->name->setText(ch.name); meter->name->setToolTip(ch.name);
            meter->values[0]->setText(number(m.vpp,ch.unit)); meter->values[1]->setText(number(m.frequency,UNIT_HERTZ));
            meter->values[2]->setText(number(m.rms,ch.unit)); meter->state->setText(state);
            meter->state->setStyleSheet(QString("color: %1; font-size: 11px;")
                .arg(!m.count || m.clipped || m.undersampled || m.irregular?"#ffc184":"#a6b9cf"));
            meter->span->setText(tr("%1 · %2 samples").arg(spanMode->currentText()).arg(m.count)); meter->show();
        }
        if(reference && !difference.error.isEmpty()) state+=" · "+difference.error;
        const auto &channelStats=statistics.channels[c];
        const auto &stats=statisticMetric->currentIndex()==2?channelStats.frequency:statisticMetric->currentIndex()==1?channelStats.rms:channelStats.vpp;
        const auto statsUnit=statisticMetric->currentIndex()==2?UNIT_HERTZ:ch.unit;
        const QString statisticsText=statistics.matches(*selected,span,align->isChecked())?
            QString("n=%1 · mean %2 · σ %3\nmin %4 · max %5").arg(stats.count)
                .arg(number(stats.count?stats.mean:unavailable,statsUnit),number(stats.deviation(),statsUnit),number(stats.minimum,statsUnit),number(stats.maximum,statsUnit)):
            tr("No matching live statistics");
        auto percent=[](double v){return std::isfinite(v)?QString::number(100*v,'f',2)+" %":QString::fromUtf8("—");};
        const QString extent=m.count?tr("%1 to %2").arg(number(origin+range.begin*ch.signal.interval,UNIT_SECONDS),
            number(origin+(range.end-1)*ch.signal.interval,UNIT_SECONDS)):tr("Empty span");
        const QStringList cells={QString("%1 · %2\n%3 samples · %4").arg(ch.name,extent).arg(m.count).arg(number(m.span,UNIT_SECONDS)),
            number(m.vpp,ch.unit),number(m.mean,ch.unit),number(m.rms,ch.unit),number(m.frequency,UNIT_HERTZ),
            percent(m.duty),
            number(m.positiveWidth,UNIT_SECONDS),number(m.rise,UNIT_SECONDS),number(m.fall,UNIT_SECONDS),
            number(difference.count?difference.rms:unavailable,ch.unit),number(difference.count?difference.maximum:unavailable,ch.unit),state,statisticsText,
            number(m.minimum,ch.unit),number(m.maximum,ch.unit),number(m.acRms,ch.unit),number(m.period,UNIT_SECONDS),
            percent(m.negativeDuty),number(m.negativeWidth,UNIT_SECONDS),number(m.cycleMean,ch.unit),number(m.cycleRms,ch.unit),number(m.crestFactor)};
        for(int col=0;col<cells.size();++col) measurements->setItem(int(c),col,new QTableWidgetItem(cells[col]));
        measurements->item(int(c),0)->setForeground(channelColor(c));
        const QString cycles=tr("%1 complete cycles over %2 inside the measurement span. Mean/RMS integrate linearly interpolated samples.")
            .arg(m.cycles).arg(number(m.cycleSpan,UNIT_SECONDS));
        measurements->item(int(c),19)->setToolTip(cycles); measurements->item(int(c),20)->setToolTip(cycles);
        const QString overlap=tr("%1 selected samples with reference overlap inside the measurement span.").arg(difference.count);
        measurements->item(int(c),9)->setToolTip(overlap); measurements->item(int(c),10)->setToolTip(overlap);
    }
    measurements->resizeRowsToContents();
    status->setText(tr("%1 / 128 displayed acquisitions · %2 MiB / 64 MiB · %3 skipped acquisition tags · NOT gapless. "
                       "Measurements: %4; shaded span, sample centres inside its boundaries. Δ: reference overlap within this span. "
                       "Live statistics: newly recorded, unclipped acquisitions only; browsing does not add values.")
                       .arg(history.frames().size()).arg(double(history.bytes())/1048576,0,'f',1).arg(history.skippedTags()).arg(spanMode->currentText())+
        tr(" %1.").arg(align->isChecked()?tr("Time zero: trigger when available, otherwise record start"):tr("Time zero: record start"))+
        (selected?tr(" Selected #%1%2.").arg(selected->tag).arg(live->isChecked()?tr(" (latest)"):tr(" (frozen; live scope is unchanged)")):QString())+
        (reference?tr(" Reference #%1.").arg(reference->tag):QString()));
    status->setToolTip(status->text());
    status->setText(tr("%1 / 128 captures   ·   %2 / 64 MiB   ·   %3 skipped tags   ·   %4   ·   %5\nDisplayed acquisitions only — not gapless. Statistics count new recordings, not history navigation.")
        .arg(history.frames().size()).arg(double(history.bytes())/1048576,0,'f',1).arg(history.skippedTags())
        .arg(spanMode->currentText(),reference?tr("Reference #%1").arg(reference->tag):tr("No reference pinned")));
}
void CaptureDock::saveCapture() {
    if(!selected) {QMessageBox::information(this,tr("Save capture"),tr("Select an acquisition first.")); return;}
    const auto snapshot=selected; // A nested file dialog must not change which acquisition is saved.
    QFileDialog dialog(this,tr("Save capture"),"capture.ohl.json",tr("Lab captures (*.ohl.json)"));
    dialog.setAcceptMode(QFileDialog::AcceptSave); dialog.setDefaultSuffix("ohl.json");
    if(dialog.exec()!=QDialog::Accepted) return;
    const auto path=dialog.selectedFiles().first();
    QString error;
    if(!snapshot->save(path,error)) QMessageBox::warning(this,tr("Save capture"),error);
}
void CaptureDock::openCapture() {
    auto path=QFileDialog::getOpenFileName(this,tr("Open capture"),{},tr("Lab captures (*.ohl.json);;JSON (*.json)"));
    if(path.isEmpty()) return;
    QString error; auto capture=Capture::load(path,error);
    if(!capture) {QMessageBox::warning(this,tr("Open capture"),error); return;}
    live->setChecked(false); selected=std::move(capture); refreshList(); refresh();
}
void CaptureDock::clearHistory() {
    if(history.frames().empty() && !selected) return;
    const bool recording=record->isChecked(); record->setChecked(false);
    const auto choice=QMessageBox::question(this,tr("Clear capture history"),
        tr("Remove the retained captures and selected view from this session?\nSaved files, the pinned reference and the measurement log are kept.\nHistory recording is paused while you decide."),
        QMessageBox::Yes|QMessageBox::Cancel,QMessageBox::Cancel);
    if(choice==QMessageBox::Yes) {history.clear(); selected.reset(); statistics.clear(); resetMask();}
    record->setChecked(recording);
}
bool CaptureDock::exportLog() {
    if(measurementLog.entries().empty()) {QMessageBox::information(this,tr("Export log"),tr("Collect some measurements first.")); return false;}
    const auto snapshot=measurementLog; // Live logging can continue inside the nested file dialog.
    const auto revision=logRevision;
    QFileDialog dialog(this,tr("Export measurement log"),"measurements.csv",tr("Measurement log (*.csv)"));
    dialog.setAcceptMode(QFileDialog::AcceptSave); dialog.setDefaultSuffix("csv");
    if(dialog.exec()!=QDialog::Accepted) return false;
    QString error;
    if(!snapshot.saveCsv(dialog.selectedFiles().first(),error)) {QMessageBox::warning(this,tr("Export log"),error); return false;}
    exportedLogRevision=revision;
    refresh();
    return true;
}
bool CaptureDock::confirmDiscardLog() {
    if(measurementLog.entries().empty() || logRevision==exportedLogRevision) return true;
    const bool collecting=collectLog->isChecked(); collectLog->setChecked(false);
    const auto choice=QMessageBox::warning(this,tr("Unexported measurement log"),
        tr("The session log has unexported readings (%1 retained). Export them before discarding?\nCollection is paused while you decide.")
            .arg(measurementLog.entries().size()),QMessageBox::Save|QMessageBox::Discard|QMessageBox::Cancel,QMessageBox::Save);
    if(choice==QMessageBox::Discard || (choice==QMessageBox::Save && exportLog())) return true;
    collectLog->setChecked(collecting); return false;
}
}
