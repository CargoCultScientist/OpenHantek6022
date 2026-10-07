// SPDX-License-Identifier: GPL-2.0-or-later
#include "capturedock.h"
#include "dsosettings.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTableWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <functional>

namespace Lab {
static QString number(double value, Unit unit=UNIT_NONE) {
    return std::isfinite(value) ? valueToString(value,unit,5) : QString::fromUtf8("—");
}
class CapturePlot : public QWidget {
public:
    using QWidget::QWidget;
    std::shared_ptr<const Capture> current, reference;
    bool align=false;
    double offset=0;
    double zoom=1, pan=0;
    TimeSpan gate;
    std::function<void()> viewChanged;
    std::function<void(double,bool)> cursorPlaced;
    QPoint dragStart;
    double dragPan=0;
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
    void mouseDoubleClickEvent(QMouseEvent *) override {zoom=1;pan=0;update();if(viewChanged) viewChanged();}
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.fillRect(rect(),QColor("#101a27"));
        p.setPen(QColor("#b6c6d6"));
        if(!current) {p.drawText(rect(),Qt::AlignCenter,tr("Waiting for a displayed acquisition")); return;}
        const std::array<QColor,3> colors={QColor("#fbd46a"),QColor("#68c8ee"),QColor("#d6a0ed")};
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
            p.setPen(colors[size_t(c)]);
            p.drawText(QRectF(4,area.top()-20,85,20),Qt::AlignLeft,ch.name.left(18));
            p.setPen(QColor("#b6c6d6"));
            p.drawText(QRectF(4,area.top(),85,20),Qt::AlignLeft,number(high,ch.unit));
            p.drawText(QRectF(4,area.bottom()-20,85,20),Qt::AlignLeft,number(low,ch.unit));
            p.drawText(QRectF(area.left(),area.bottom()+2,160,20),number(start,UNIT_SECONDS));
            p.drawText(QRectF(area.right()-160,area.bottom()+2,160,20),Qt::AlignRight,number(end,UNIT_SECONDS));
            auto draw=[&](const CaptureChannel &channel,double origin,bool dashed) {
                if(channel.signal.samples.empty() || !(channel.signal.interval>0)) return;
                p.save(); p.setClipRect(area);
                QPen pen(colors[size_t(c)],dashed?1.0:1.5,dashed?Qt::DashLine:Qt::SolidLine); p.setPen(pen);
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
    auto controls=new QHBoxLayout;
    record=new QCheckBox(tr("Record history"),body); record->setChecked(true);
    live=new QCheckBox(tr("Follow latest"),body); live->setChecked(true);
    controls->addWidget(record); controls->addWidget(live);
    auto button=[&](const QString &text,auto callback) {
        auto b=new QPushButton(text,body); controls->addWidget(b); connect(b,&QPushButton::clicked,this,callback); return b;
    };
    button(tr("Save capture…"),[this]{saveCapture();});
    button(tr("Open capture…"),[this]{openCapture();});
    button(tr("Set reference"),[this]{reference=selected; refresh();});
    button(tr("Clear reference"),[this]{reference.reset(); refresh();});
    button(tr("Clear history"),[this]{history.clear(); selected.reset(); statistics.clear(); refreshList(); refresh();});
    controls->addStretch(); layout->addLayout(controls);
    auto comparison=new QHBoxLayout;
    align=new QCheckBox(tr("Align trigger"),body);
    align->setToolTip(tr("Time zero is the trigger when available, otherwise the record start. Reference comparison requires both captures to be triggered."));
    comparison->addWidget(align);
    comparison->addWidget(new QLabel(tr("Reference time shift (s):"),body));
    offset=new QDoubleSpinBox(body); offset->setDecimals(9); offset->setRange(-1e3,1e3); offset->setSingleStep(.000001);
    comparison->addWidget(offset);
    comparison->addWidget(new QLabel(tr("Solid: selected · Dashed: reference · Wheel: zoom · Drag: pan · Double-click: reset"),body));
    comparison->addStretch(); layout->addLayout(comparison);
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
    measurementControls->addStretch(); layout->addLayout(measurementControls);
    auto statisticsControls=new QHBoxLayout;
    statisticsControls->addWidget(new QLabel(tr("Live statistics:"),body));
    statisticMetric=new QComboBox(body); statisticMetric->setObjectName("labStatisticMetric");
    statisticMetric->addItems({tr("Vpp"),tr("RMS"),tr("Frequency")}); statisticsControls->addWidget(statisticMetric);
    auto reset=new QPushButton(tr("Reset statistics"),body); reset->setObjectName("labResetStatistics");
    statisticsControls->addWidget(reset);
    statisticsControls->addWidget(new QLabel(tr("New recorded acquisitions only · Reset on span or setup change"),body));
    statisticsControls->addStretch(); layout->addLayout(statisticsControls);
    auto splitter=new QSplitter(body);
    list=new QListWidget(splitter); list->setMinimumWidth(160); list->setMaximumWidth(270);
    plot=new CapturePlot(splitter); plot->setObjectName("labCapturePlot"); plot->setMinimumSize(420,180); splitter->setStretchFactor(1,1);
    plot->viewChanged=[this]{if(spanMode->currentIndex()==1) resetStatistics(); else refresh();};
    plot->cursorPlaced=[this](double value,bool second){
        (second?cursorB:cursorA)->setValue(value); spanMode->setCurrentIndex(2);
    };
    layout->addWidget(splitter,1);
    measurements=new QTableWidget(body);
    measurements->setObjectName("labMeasurements"); measurements->setColumnCount(22);
    measurements->setHorizontalHeaderLabels({tr("Channel / span"),tr("Vpp"),tr("Mean"),tr("RMS"),tr("Frequency"),tr("Duty +"),
        tr("Width +"),tr("Rise 10–90"),tr("Fall 90–10"),tr("Δ RMS"),tr("Δ max"),tr("Status"),tr("Live Vpp statistics"),
        tr("Minimum"),tr("Maximum"),tr("AC RMS"),tr("Period"),tr("Duty −"),tr("Width −"),tr("Cycle mean"),tr("Cycle RMS"),tr("Crest factor")});
    for(int c=13;c<measurements->columnCount();++c) measurements->setColumnHidden(c,true);
    measurements->setEditTriggers(QAbstractItemView::NoEditTriggers);
    measurements->setSelectionBehavior(QAbstractItemView::SelectRows);
    measurements->verticalHeader()->hide();
    measurements->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    measurements->setMinimumHeight(145); measurements->setMaximumHeight(235);
    layout->addWidget(measurements);
    status=new QLabel(body); status->setWordWrap(true); layout->addWidget(status);
    setWidget(body);
    connect(list,&QListWidget::currentRowChanged,this,[this](int row) {
        if(row<0 || size_t(row)>=history.frames().size()) return;
        live->setChecked(false); selected=history.frames()[size_t(row)]; refresh();
    });
    connect(live,&QCheckBox::toggled,this,[this](bool enabled){
        if(enabled && !history.frames().empty()) {selected=history.frames().back(); refreshList(); refresh();}
    });
    connect(align,&QCheckBox::toggled,this,[this]{resetStatistics();});
    connect(offset,qOverload<double>(&QDoubleSpinBox::valueChanged),this,[this]{refresh();});
    connect(spanMode,qOverload<int>(&QComboBox::currentIndexChanged),this,[this](int mode){
        cursorA->setEnabled(mode==2); cursorB->setEnabled(mode==2); resetStatistics();
    });
    for(auto box:{cursorA,cursorB}) connect(box,qOverload<double>(&QDoubleSpinBox::valueChanged),this,[this]{
        if(spanMode->currentIndex()==2) resetStatistics();
    });
    connect(reset,&QPushButton::clicked,this,&CaptureDock::resetStatistics);
    connect(statisticMetric,qOverload<int>(&QComboBox::currentIndexChanged),this,[this]{refresh();});
    connect(details,&QCheckBox::toggled,this,[this](bool enabled){
        for(int c=13;c<measurements->columnCount();++c) measurements->setColumnHidden(c,!enabled);
    });
    connect(this,&QDockWidget::visibilityChanged,this,[this](bool visible){if(visible) refresh();});
    refresh();
}
void CaptureDock::ingest(const std::shared_ptr<PPresult> &frame) {
    if(!record->isChecked() || !frame) return;
    // Do not copy repeated redraws of a stopped/held acquisition.
    if(!history.frames().empty() && history.frames().back()->tag==frame->tag && history.frames().back()->capturedAtMs==frame->capturedAtMs) return;
    auto capture=Capture::fromResult(*frame,*settings);
    if(!history.append(capture)) return;
    if(live->isChecked()) selected=capture;
    plot->current=selected; plot->align=align->isChecked();
    statistics.add(*capture,measurementSpan(),align->isChecked());
    refreshList();
    if(isVisible()) refresh();
}
TimeSpan CaptureDock::measurementSpan() const {
    if(spanMode->currentIndex()==1) return plot->visibleSpan();
    if(spanMode->currentIndex()==2) return {std::min(cursorA->value(),cursorB->value()),std::max(cursorA->value(),cursorB->value())};
    return {};
}
void CaptureDock::resetStatistics() {statistics.clear(); refresh();}
void CaptureDock::refreshList() {
    QSignalBlocker blocker(list); list->clear();
    int index=0;
    for(const auto &frame:history.frames()) {
        list->addItem(QString("#%1  %2").arg(frame->tag).arg(QDateTime::fromMSecsSinceEpoch(frame->capturedAtMs).toString("HH:mm:ss.zzz")));
        if(frame==selected) list->setCurrentRow(index);
        ++index;
    }
    if(live->isChecked()) list->scrollToBottom();
}
void CaptureDock::refresh() {
    plot->current=selected; plot->reference=reference; plot->align=align->isChecked(); plot->offset=offset->value();
    const auto lanes=selected?std::count_if(selected->channels.begin(),selected->channels.end(),[](const auto &ch){return !ch.signal.samples.empty();}):0;
    plot->setMinimumHeight(std::max(180,30+int(lanes)*74));
    const auto span=measurementSpan(); plot->gate=span; plot->update();
    measurements->horizontalHeaderItem(12)->setText(tr("Live %1 statistics").arg(statisticMetric->currentText()));
    measurements->setRowCount(selected?int(selected->channels.size()):0);
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
}
