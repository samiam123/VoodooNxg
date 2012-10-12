/* Copyright (c) 2006-2010, Linden Research, Inc.
 * 
 * LLQtWebKit Source Code
 * The source code in this file ("Source Code") is provided by Linden Lab
 * to you under the terms of the GNU General Public License, version 2.0
 * ("GPL"), unless you have obtained a separate licensing agreement
 * ("Other License"), formally executed by you and Linden Lab.  Terms of
 * the GPL can be found in GPL-license.txt in this distribution, or online at
 * http://secondlifegrid.net/technology-programs/license-virtual-world/viewerlicensing/gplv2
 * 
 * There are special exceptions to the terms and conditions of the GPL as
 * it is applied to this Source Code. View the full text of the exception
 * in the file FLOSS-exception.txt in this software distribution, or
 * online at
 * http://secondlifegrid.net/technology-programs/license-virtual-world/viewerlicensing/flossexception
 * 
 * By copying, modifying or distributing this software, you acknowledge
 * that you have read and understood your obligations described above,
 * and agree to abide by those obligations.
 * 
 * ALL LINDEN LAB SOURCE CODE IS PROVIDED "AS IS." LINDEN LAB MAKES NO
 * WARRANTIES, EXPRESS, IMPLIED OR OTHERWISE, REGARDING ITS ACCURACY,
 * COMPLETENESS OR PERFORMANCE.
 */

#include "llstyle.h"

#include "llembeddedbrowserwindow_p.h"
#include <qstyleoption.h>
#include <qpainter.h>
#include <qdebug.h>

LLStyle::LLStyle()
    : QPlastiqueStyle()
{
}

void LLStyle::drawComplexControl(ComplexControl control, const QStyleOptionComplex *option, QPainter *painter, const QWidget *widget) const
{
#ifdef Q_WS_MAC
    if (control == QStyle::CC_ScrollBar) {
            QStyleOptionSlider* opt = (QStyleOptionSlider*)option;
            const QPoint topLeft = opt->rect.topLeft();
            painter->translate(topLeft);
            opt->rect.moveTo(QPoint(0, 0));
            painter->fillRect(opt->rect, opt->palette.background());
    }
#endif
    QPlastiqueStyle::drawComplexControl(control, option, painter, widget);
}

void LLStyle::drawControl(ControlElement element, const QStyleOption *option, QPainter *painter, const QWidget *widget) const
{
	switch(element)
	{
	case CE_ScrollBarAddLine:
	case CE_ScrollBarSubLine:
		// This fixes the "scrollbar arrows pointing the wrong way" bug.
        if (const QStyleOptionSlider *scrollBar = qstyleoption_cast<const QStyleOptionSlider *>(option))
		{
			// Make the State_Horizontal bit in the option's state field match its orientation field.
			QStyleOptionSlider localOption(*scrollBar);
            if(localOption.orientation == Qt::Horizontal)
			{
				localOption.state |= State_Horizontal;
			}
			else
			{
				localOption.state &= ~State_Horizontal;
			}
			QPlastiqueStyle::drawControl(element, &localOption, painter, widget);	
			return;
		}

	break;
	}
	
    QPlastiqueStyle::drawControl(element, option, painter, widget);	
}
