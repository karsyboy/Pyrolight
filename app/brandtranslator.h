#pragma once

#include <QByteArray>
#include <QRegularExpression>
#include <QTranslator>

// Keep upstream translation keys/catalogs unchanged. Apply the client brand after
// lookup, including the English fallback, while preserving names of external tools.
class BrandTranslator : public QTranslator
{
public:
    bool isEmpty() const override { return false; }

    QString translate(const char* context, const char* sourceText,
                      const char* disambiguation = nullptr, int n = -1) const override
    {
        QString text = QTranslator::translate(context, sourceText, disambiguation, n);
        if (text.isNull() && sourceText != nullptr &&
                QByteArray(sourceText).contains("Moonlight")) {
            text = QString::fromUtf8(sourceText);
        }
        static const QRegularExpression clientName(QStringLiteral("\\bMoonlight\\b(?! Internet Hosting Tool)"));
        return text.replace(clientName, QStringLiteral("Pyrolight"));
    }
};
