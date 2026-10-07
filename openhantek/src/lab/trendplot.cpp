// SPDX-License-Identifier: GPL-2.0-or-later
#include "trendplot.h"
#include <QDateTime>
#include <QPainter>
#include <algorithm>
#include <cmath>

namespace Lab {
void TrendPlot::paintEvent(QPaintEvent *) {
    QPainter painter(this); painter.fillRect(rect(),QColor("#101a27")); painter.setPen(QColor("#b6c6d6"));
    if(log->entries().empty()) {
        painter.drawText(rect().adjusted(20,20,-20,-20),Qt::AlignCenter|Qt::TextWordWrap,
            tr("No trend yet\nEnable Collect measurements in the Session log tab.\nLogging is RAM-only until you export CSV.")); return;
    }
    const auto &rows=log->entries(); const auto &latest=rows.back();
    auto first=rows.end()-1;
    while(first!=rows.begin() && (first-1)->segment==latest.segment) --first;
    auto value=[this](const MeasurementLogEntry &row) {
        if(row.values.clipped) return unavailable;
        return metric==0?row.values.vpp:metric==1?row.values.rms:metric==2?row.values.frequency:row.values.mean;
    };
    double low=std::numeric_limits<double>::infinity(), high=-low;
    for(auto row=first;row!=rows.end();++row) if(std::isfinite(value(*row))) {low=std::min(low,value(*row));high=std::max(high,value(*row));}
    const QString metricName=QStringList{tr("Vpp"),tr("RMS"),tr("Frequency"),tr("Mean")}.at(metric);
    painter.drawText(QRectF(16,4,width()-32,24),tr("%1  /  %2  ·  segment %3  ·  %4 retained readings")
        .arg(latest.channel,metricName).arg(latest.segment).arg(rows.end()-first));
    if(!std::isfinite(low)) {painter.drawText(rect(),Qt::AlignCenter,tr("No valid readings for this metric in the latest segment"));return;}
    const double padding=std::max((high-low)*.15,std::max(std::abs(low)*.01,1e-9)); low-=padding;high+=padding;
    const Unit unit=metric==2?UNIT_HERTZ:latest.unit;
    const QRectF area(100,35,std::max(1,width()-122),std::max(1,height()-74));
    painter.setPen(QColor("#2b3f55"));
    for(int i=0;i<=4;++i) {
        const double y=area.top()+area.height()*i/4;
        painter.drawLine(QPointF(area.left(),y),QPointF(area.right(),y));
        painter.setPen(QColor("#b6c6d6"));
        painter.drawText(QRectF(2,y-10,91,20),Qt::AlignRight,valueToString(high-(high-low)*i/4,unit,4));
        painter.setPen(QColor("#2b3f55"));
    }
    const qint64 start=first->capturedAtMs, end=latest.capturedAtMs;
    painter.setPen(QColor("#b6c6d6"));
    painter.drawText(QRectF(area.left(),area.bottom()+4,180,22),QDateTime::fromMSecsSinceEpoch(start).toString("HH:mm:ss.zzz"));
    painter.drawText(QRectF(area.right()-180,area.bottom()+4,180,22),Qt::AlignRight,QDateTime::fromMSecsSinceEpoch(end).toString("HH:mm:ss.zzz"));
    painter.save(); painter.setClipRect(area.adjusted(-2,-2,2,2));
    painter.setPen(QPen(QColor("#8fe8d4"),1.6));
    const int pixels=std::max(1,int(area.width()));
    auto pixel=[&](qint64 time){return end>start?std::clamp(int(double(time-start)/double(end-start)*(pixels-1)),0,pixels-1):pixels/2;};
    auto point=[&](int x,double v){return QPointF(area.left()+x,area.bottom()-(v-low)/(high-low)*area.height());};
    // Preserve every finite extremum in each pixel column; invalid rows break the line.
    QPointF previous; bool connected=false;
    for(auto row=first;row!=rows.end();) {
        if(!std::isfinite(value(*row))) {connected=false; ++row; continue;}
        const int x=pixel(row->capturedAtMs);
        double minimum=value(*row), maximum=minimum, firstValue=minimum, lastValue=minimum;
        auto next=row+1;
        while(next!=rows.end() && pixel(next->capturedAtMs)==x && std::isfinite(value(*next))) {
            lastValue=value(*next); minimum=std::min(minimum,lastValue); maximum=std::max(maximum,lastValue); ++next;
        }
        painter.drawLine(point(x,minimum),point(x,maximum));
        if(connected) painter.drawLine(previous,point(x,firstValue));
        else painter.drawEllipse(point(x,firstValue),2,2);
        previous=point(x,lastValue); connected=true; row=next;
    }
    painter.restore();
}
}
