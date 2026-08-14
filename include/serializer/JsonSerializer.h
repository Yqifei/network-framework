#pragma once

#include <QByteArray>
#include <QFlags>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QMetaProperty>
#include <QMetaType>
#include <QObject>
#include <QVariant>
#include <memory>
#include <type_traits>

#include <core/global.h>
#include <serializer/SerializerException.h>

namespace NetCore {
namespace internal
{
template <typename T, typename = void>
struct HasStaticMetaObject : std::false_type
{
};

template <typename T>
struct HasStaticMetaObject<T, std::void_t<decltype(T::staticMetaObject)>> : std::true_type
{
};

template <typename T, typename = void>
struct IsQMetaTypeRegistered : std::false_type
{
};

template <typename T>
struct IsQMetaTypeRegistered<T, std::void_t<decltype(qMetaTypeId<T>())>> : std::true_type
{
};

template <typename T>
struct QtReflectionCheck
{
    static_assert(HasStaticMetaObject<T>::value, "The type must have the Q_OBJECT or Q_GADGET macro");
    static_assert(IsQMetaTypeRegistered<T>::value, "The type must be registered using Q_DECLARE_METATYPE");
};

} // namespace internal

enum SerializerFlag {
    None = 0x00,
    AllowNullForClasses = 0x01,
    IgnoreUnknownKeys = 0x02,
    EnumAsString = 0x04,
    ValidationEnabled = 0x08
};
Q_DECLARE_FLAGS(SerializerFlags, SerializerFlag)
Q_DECLARE_OPERATORS_FOR_FLAGS(SerializerFlags)

class NETWORK_EXPORT JsonSerializer : public QObject
{
    Q_OBJECT

public:
    explicit JsonSerializer(QObject *parent = nullptr);

    SerializerFlags flags() const { return m_flags; }
    void setFlags(SerializerFlags flags) { m_flags = flags; }

    template <typename T>
    QJsonObject serialize(const T &data) const
    {
        internal::QtReflectionCheck<T> check;
        Q_UNUSED(check)
        return serializeInternal(&T::staticMetaObject, &data);
    }

    template <typename T>
    QByteArray serializeToBytes(const T &data) const
    {
        return QJsonDocument(serialize<T>(data)).toJson(QJsonDocument::Compact);
    }

    // Q_GADGET（值类型）：按值反序列化
    template <typename T, typename std::enable_if_t<!std::is_base_of_v<QObject, T>, int> = 0>
    T deserialize(const QJsonObject &object, QObject *parent = nullptr) const
    {
        internal::QtReflectionCheck<T> check;
        Q_UNUSED(check)
        Q_UNUSED(parent)

        T instance;
        deserializeInternal(object, &T::staticMetaObject, &instance);
        return instance;
    }

    // QObject 子类：堆上创建，生命周期交给 parent（Qt 父子机制）
    template <typename T, typename std::enable_if_t<std::is_base_of_v<QObject, T>, int> = 0>
    T *deserialize(const QJsonObject &object, QObject *parent = nullptr) const
    {
        internal::QtReflectionCheck<T> check;
        Q_UNUSED(check)

        // 异常安全：填充失败自动释放，成功后把所有权交给调用方
        std::unique_ptr<T> instance(new T(parent));
        deserializeInternal(object, &T::staticMetaObject, instance.get());
        return instance.release();
    }

    template <typename T, typename std::enable_if_t<!std::is_base_of_v<QObject, T>, int> = 0>
    T deserializeFromBytes(const QByteArray &bytes, QObject *parent = nullptr) const
    {
        return deserialize<T>(parseObject(bytes), parent);
    }

    template <typename T, typename std::enable_if_t<std::is_base_of_v<QObject, T>, int> = 0>
    T *deserializeFromBytes(const QByteArray &bytes, QObject *parent = nullptr) const
    {
        return deserialize<T>(parseObject(bytes), parent);
    }

private:
    QJsonObject parseObject(const QByteArray &bytes) const;
    QJsonObject serializeInternal(const QMetaObject *metaObject, const void *data) const;
    QJsonValue serializeQVariant(const QVariant &value, int typeId) const;
    void deserializeInternal(const QJsonObject &object, const QMetaObject *metaObject, void *data) const;
    QVariant deserializeQVariant(const QJsonValue &value, int typeId) const;

    SerializerFlags m_flags;
};

} // namespace NetCore
