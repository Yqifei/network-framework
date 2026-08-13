#pragma once

#include <QByteArray>
#include <QException>

namespace NetCore {
class SerializerException : public QException
{
public:
    explicit SerializerException(const QByteArray& message);

    void raise() const override;
    SerializerException* clone() const override;
    const char* what() const noexcept override;

private:
    QByteArray m_message;
};

class DeserializerException : public QException
{
public:
    explicit DeserializerException(const QByteArray& message);

    void raise() const override;
    DeserializerException* clone() const override;
    const char* what() const noexcept override;

private:
    QByteArray m_message;
};
}