/****************************************************************************
**
** This file taken from the examples of the Qt Toolkit, and modified as needed.
** Original license: https://www.qt.io/licensing/
** Original copyright: Copyright (C) 2016 The Qt Company Ltd.
**
** This file is licensed under BSD 3-clause license.
**
****************************************************************************/

#ifndef TWILIGHT_CLIENT_FLOWLAYOUT_H
#define TWILIGHT_CLIENT_FLOWLAYOUT_H

#include <QtCore/QRect>
#include <QtWidgets/QLayout>
#include <QtWidgets/QStyle>

class FlowLayout : public QLayout {
public:
    explicit FlowLayout(QWidget *parent, int margin = -1, int hSpacing = -1, int vSpacing = -1);
    explicit FlowLayout(int margin = -1, int hSpacing = -1, int vSpacing = -1);
    ~FlowLayout();

    void addItem(QLayoutItem *item) override;
    int horizontalSpacing() const;
    int verticalSpacing() const;
    Qt::Orientations expandingDirections() const override;
    bool hasHeightForWidth() const override;
    int heightForWidth(int) const override;
    int count() const override;
    QLayoutItem *itemAt(int index) const override;
    QSize minimumSize() const override;
    void setGeometry(const QRect &rect) override;
    QSize sizeHint() const override;
    QLayoutItem *takeAt(int index) override;

private:
    int doLayout(const QRect &rect, bool testOnly) const;
    int smartSpacing(QStyle::PixelMetric pm) const;

    QList<QLayoutItem *> itemList;
    int m_hSpace;
    int m_vSpace;
};

#endif