#include <serializer/SerializerException.h>

namespace NetCore {

    SerializerException::SerializerException(const QByteArray& message)
        : m_message(message)
    {
    }

    void SerializerException::raise() const
    {
        throw* this;
    }

    SerializerException* SerializerException::clone() const
    {
        return new SerializerException(*this);
    }

    const char* SerializerException::what() const noexcept
    {
        return m_message.constData();
    }

    DeserializerException::DeserializerException(const QByteArray& message)
        : m_message(message)
    {
    }

    void DeserializerException::raise() const
    {
        throw* this;
    }

    DeserializerException* DeserializerException::clone() const
    {
        return new DeserializerException(*this);
    }

    const char* DeserializerException::what() const noexcept
    {
        return m_message.constData();
    }

}