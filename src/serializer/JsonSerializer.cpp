#include <serializer/JsonSerializer.h>

#include <QJsonArray>
#include <QJsonValue>
#include <QMetaEnum>
#include <QSequentialIterable>
#include <QStringList>
#include <memory>

namespace {
	QByteArray listElementTypeName(const QByteArray& typeName)
	{
		if (typeName.startsWith("QList<") && typeName.endsWith('>'))
			return typeName.mid(6, typeName.size() - 7);
		if (typeName == "QStringList")
			return QByteArray("QString");
		if (typeName.startsWith("QVector<") && typeName.endsWith('>'))
			return typeName.mid(8, typeName.size() - 9);
		return QByteArray();
	}
} // namespace

namespace NetCore {
	JsonSerializer::JsonSerializer(QObject* parent)
		: QObject(parent)
		, m_flags(IgnoreUnknownKeys)
	{
	}

	QJsonObject JsonSerializer::parseObject(const QByteArray& bytes) const
	{
		QJsonParseError error;
		const QJsonDocument document = QJsonDocument::fromJson(bytes, &error);
		if (error.error != QJsonParseError::NoError) {
			throw DeserializerException("Invalid JSON: " + error.errorString().toUtf8());
		}
		if (!document.isObject()) {
			throw DeserializerException("The top-level must be a JSON object");
		}
		return document.object();
	}

	QJsonObject JsonSerializer::serializeInternal(const QMetaObject* metaObject, const void* data) const
	{
		QJsonObject object;
		const bool isQObject = metaObject->inherits(&QObject::staticMetaObject);

		for (int i = metaObject->propertyOffset(); i < metaObject->propertyCount(); ++i) {
			const QMetaProperty property = metaObject->property(i);
			if (!property.isReadable())
				continue;
			const char* name = property.name();
			if (qstrcmp(name, "objectName") == 0)
				continue;

			const QVariant value = isQObject
				? property.read(static_cast<const QObject*>(data))
				: property.readOnGadget(data);

			if (!value.isValid()) {
				if (m_flags.testFlag(AllowNullForClasses))
					object.insert(QString::fromLatin1(name), QJsonValue(QJsonValue::Null));
				continue;
			}

			if (property.isEnumType()) {
				if (m_flags.testFlag(EnumAsString)) {
					const char* key = property.enumerator().valueToKey(value.toInt());
					if (key) {
						object.insert(QString::fromLatin1(name), QString::fromLatin1(key));
						continue;
					}
				}
				object.insert(QString::fromLatin1(name), QJsonValue(value.toInt()));
				continue;
			}

			object.insert(QString::fromLatin1(name), serializeQVariant(value, value.userType()));
		}
		return object;
	}

	QJsonValue JsonSerializer::serializeQVariant(const QVariant& value, int typeId) const
	{
		switch (typeId) {
		case QMetaType::Bool:      return { value.toBool() };
		case QMetaType::Int:
		case QMetaType::Short:
		case QMetaType::SChar:     return { value.toInt() };
		case QMetaType::UInt:
		case QMetaType::UShort:
		case QMetaType::UChar:     return { static_cast<int>(value.toUInt()) };
		case QMetaType::Long:
		case QMetaType::LongLong:  return { value.toLongLong() };
		case QMetaType::ULong:
		case QMetaType::ULongLong: return { static_cast<qint64>(value.toULongLong()) };
		case QMetaType::Float:     return { static_cast<double>(value.toFloat()) };
		case QMetaType::Double:    return { value.toDouble() };
		case QMetaType::QString:   return { value.toString() };
		case QMetaType::QByteArray:return { QString::fromUtf8(value.toByteArray()) };
		case QMetaType::QJsonObject: return { value.toJsonObject() };
		case QMetaType::QJsonArray:  return { value.toJsonArray() };
		case QMetaType::QJsonValue:  return { value.toJsonValue() };
		default: break;
		}

		// 嵌套 QObject* 属性：QVariant 里存的是指针，解引用后用多态 metaObject
		if (QMetaType::typeFlags(typeId).testFlag(QMetaType::PointerToQObject)) {
			const QObject* object = value.value<QObject*>();
			if (!object)
				return QJsonValue(QJsonValue::Null);
			return QJsonValue(serializeInternal(object->metaObject(), object));
		}

		// 嵌套 Q_GADGET：按值递归序列化
		const QMetaObject* metaObject = QMetaType::metaObjectForType(typeId);
		if (metaObject)
			return { serializeInternal(metaObject, value.constData()) };

		// 列表类型：解析 typeName 提取元素类型（QList<Element> / QVector<Element> / QStringList）
		const QByteArray typeName = QMetaType::typeName(typeId);
		const QByteArray elementName = listElementTypeName(typeName);

		if (!elementName.isEmpty()) {
			const int elementTypeId = QMetaType::type(elementName.constData());
			if (elementTypeId == QMetaType::UnknownType)
				throw SerializerException("Unknown list element type: " + elementName);

			QJsonArray array;
			const QSequentialIterable iterable = value.value<QSequentialIterable>();
			for (const QVariant& item : iterable)
				array.append(serializeQVariant(item, item.userType()));
			return QJsonValue(array);
		}

		// 兜底: 对于其他 QMetaType 注册的类型，优先尝试数值再尝试字符串
		QVariant v = value;
		if (v.canConvert(QMetaType::Double)) return { v.toDouble() };
		if (v.canConvert(QMetaType::QString)) return { v.toString() };

		throw SerializerException("Unsupported property type: " + typeName);
	}

	void JsonSerializer::deserializeInternal(const QJsonObject& object, const QMetaObject* metaObject, void* data) const
	{
		const bool isQObject = metaObject->inherits(&QObject::staticMetaObject);

		// 严格模式：未知字段整体校验前置，先验证再写入
		if (!m_flags.testFlag(IgnoreUnknownKeys)) {
			for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
				if (metaObject->indexOfProperty(it.key().toLatin1().constData()) < 0)
					throw DeserializerException("Unknown field: " + it.key().toUtf8());
			}
		}

		for (int i = metaObject->propertyOffset(); i < metaObject->propertyCount(); ++i) {
			const QMetaProperty property = metaObject->property(i);
			if (!property.isWritable())
				continue;
			const char* name = property.name();
			if (qstrcmp(name, "objectName") == 0)
				continue;

			const QString key = QString::fromLatin1(name);
			if (!object.contains(key)) {
				if (m_flags.testFlag(ValidationEnabled) && property.isRequired())
					throw DeserializerException("Missing field: " + key.toUtf8());
				continue;
			}

			const QJsonValue jsonValue = object.value(key);
			if (jsonValue.isNull()
				&& QMetaType::typeFlags(property.userType()).testFlag(QMetaType::PointerToQObject)) {
				continue;   // 指针属性：JSON null 保持默认空指针
			}

			QVariant value;

			if (property.isEnumType() && m_flags.testFlag(EnumAsString) && jsonValue.isString()) {
				bool ok = false;
				const int enumValue = property.enumerator().keyToValue(
					jsonValue.toString().toLatin1().constData(), &ok);
				if (!ok)
					throw DeserializerException("Unknown enum value: " + jsonValue.toString().toUtf8());
				value = QVariant(enumValue);
			}
			else if (property.isEnumType()) {
				value = QVariant(jsonValue.toInt());
			}
			else {
				QObject* contextParent = isQObject ? static_cast<QObject*>(data) : nullptr;
				try {
					value = deserializeQVariant(jsonValue, property.userType(), contextParent);
				} catch (const DeserializerException& ex) {
					throw DeserializerException(
						"Failed to deserialize field '" + key.toUtf8() + "': " + ex.what());
				}
			}

			const bool ok = isQObject
				? property.write(static_cast<QObject*>(data), value)
				: property.writeOnGadget(data, value);
			if (!ok) {
				// 指针类型写入失败：变体里新建的对象无人接管，需手动释放
				if (QMetaType::typeFlags(property.userType()).testFlag(QMetaType::PointerToQObject))
					delete value.value<QObject*>();
				throw DeserializerException("Failed to write property: " + key.toUtf8());
			}
		}
	}

	QVariant JsonSerializer::deserializeQVariant(const QJsonValue& value, int typeId, QObject* contextParent) const
	{
		// null / undefined: 返回该类型的默认值，不走后续逻辑
		if (value.isNull() || value.isUndefined()) {
			if (m_flags.testFlag(AllowNullForClasses)
				&& QMetaType::typeFlags(typeId).testFlag(QMetaType::PointerToQObject)) {
				return QVariant::fromValue<QObject*>(nullptr);
			}
			return QVariant(typeId, nullptr);
		}

		switch (typeId) {
		case QMetaType::Bool: {
			// 容错: 服务端可能返回 0/1 或 "true"/"false" 表示布尔值
			if (value.isBool()) return QVariant(value.toBool());
			if (value.isDouble()) return QVariant(value.toDouble() != 0.0);
			if (value.isString()) {
				const QString str = value.toString().toLower();
				return QVariant(str == QLatin1String("true") || str == QLatin1String("1"));
			}
			return QVariant(value.toBool());
		}
		case QMetaType::Int:
		case QMetaType::Short:
		case QMetaType::SChar:
			return QVariant::fromValue<int>(value.toInt());
		case QMetaType::UInt:
		case QMetaType::UShort:
		case QMetaType::UChar:
			return QVariant::fromValue<unsigned int>(static_cast<unsigned int>(value.toDouble()));
		case QMetaType::Long:
		case QMetaType::LongLong:
			return QVariant::fromValue<qlonglong>(static_cast<qlonglong>(value.toDouble()));
		case QMetaType::ULong:
		case QMetaType::ULongLong:
			return QVariant::fromValue<qulonglong>(static_cast<qulonglong>(value.toDouble()));
		case QMetaType::Float:
			return QVariant::fromValue<float>(static_cast<float>(value.toDouble()));
		case QMetaType::Double:
			return QVariant(value.toDouble());
		case QMetaType::QString:
			// 容错: 数值类型也能转成 QString
			if (value.isString()) return QVariant(value.toString());
			return QVariant(value.toVariant().toString());
		case QMetaType::QByteArray:
			return QVariant(value.toString().toUtf8());
		case QMetaType::QJsonObject:
			return QVariant(value.toObject());
		case QMetaType::QJsonArray:
			return QVariant(value.toArray());
		case QMetaType::QJsonValue:
			return QVariant::fromValue(value);
		default: break;
		}

		// 嵌套 QObject* 属性：动态创建实例（构造函数需 Q_INVOKABLE）
		if (QMetaType::typeFlags(typeId).testFlag(QMetaType::PointerToQObject)) {
			if (!value.isObject())
				throw DeserializerException("Expected JSON object for nested type");

			const QMetaObject* metaObject = QMetaType::metaObjectForType(typeId);
			if (!metaObject)
				throw DeserializerException("Cannot determine concrete QObject type for deserialization");

			QObject* object = metaObject->newInstance(Q_ARG(QObject*, contextParent));
			if (!object)
				throw DeserializerException("Failed to create QObject, constructor must be Q_INVOKABLE");
			std::unique_ptr<QObject> guard(object);
			deserializeInternal(value.toObject(), metaObject, object);
			guard.release();
			return QVariant(typeId, &object);
		}

		// 嵌套 Q_GADGET：QVariant 里默认构造一个实例，递归填充
		const QMetaObject* metaObject = QMetaType::metaObjectForType(typeId);
		if (metaObject) {
			if (!value.isObject())
				throw DeserializerException("Expected JSON object for nested type");
			QVariant instance(typeId, nullptr);
			deserializeInternal(value.toObject(), metaObject, instance.data());
			return instance;
		}

		// 列表类型：构建 QVariantList 再 convert 成目标容器类型
		const QByteArray typeName = QMetaType::typeName(typeId);
		const QByteArray elementName = listElementTypeName(typeName);

		if (!elementName.isEmpty()) {
			if (!value.isArray())
				throw DeserializerException("Expected JSON array for list type");
			const int elementTypeId = QMetaType::type(elementName.constData());
			if (elementTypeId == QMetaType::UnknownType)
				throw DeserializerException("Unknown list element type: " + elementName);

			QVariantList list;
			const QJsonArray array = value.toArray();
			for (const QJsonValue& item : array)
				list.append(deserializeQVariant(item, elementTypeId, contextParent));

			QVariant result = QVariant::fromValue(list);
			if (!result.convert(typeId)) {
				// 指针元素已创建但容器转换失败：手动释放，避免泄漏
				if (QMetaType::typeFlags(elementTypeId).testFlag(QMetaType::PointerToQObject)) {
					for (const QVariant& item : list)
						delete item.value<QObject*>();
				}
				throw DeserializerException("List conversion failed, register converter for: " + typeName);
			}
			return result;
		}

		throw DeserializerException("Unsupported property type: " + typeName);
	}
} // namespace NetCore
