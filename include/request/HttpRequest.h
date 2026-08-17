#pragma once

#include <string_view>

#include "HttpRequest.h"

namespace NetCore {

	template <char... Cs>
	struct StrLiteral
	{
		static constexpr char kData[] = { Cs...,'\0' };
		static constexpr const char* c_str() { return kData; }
		static constexpr std::string_view view()
		{
			return {
				kData, sizeof...(Cs)
			};
		}
	};

	template <typename Acc, char...CArgs>
	struct BuildStr
	{
		using type = Acc;
	};

	template <char... Acc, char C, char...Rest>
	struct BuildStr<StrLiteral<Acc...>, C, Rest...>
		:BuildStr<StrLiteral<Acc..., C>, Rest...>
	{
	};

	template<char...Acc, char...Rest>
	struct BuildStr<StrLiteral<Acc...>, '\0', Rest...>
	{
		using  type = StrLiteral<Acc...>;
	};


#define _C(s, i) (sizeof(s) > (i) ? s[i] : '\0')

#define _STR_8(s, offset) \
      _C(s, offset + 0), _C(s, offset + 1), _C(s, offset + 2), _C(s, offset + 3), \
      _C(s, offset + 4), _C(s, offset + 5), _C(s, offset + 6), _C(s, offset + 7)

#define _STR_64(s) \
      _STR_8(s, 0), _STR_8(s, 8), _STR_8(s, 16), _STR_8(s, 24), \
      _STR_8(s, 32), _STR_8(s, 40), _STR_8(s, 48), _STR_8(s, 56)

#define STR(s) typename NetCore::BuildStr<NetCore::StrLiteral<>, _STR_64(s)>::type


	// ---------- HTTP 方法 ----------
	enum class HttpMethod {
		GET,
		POST,
		PUT,
		DEL,
		PATCH
	};

	// ----------参数位置标签（Tag Dispatch）----------
	struct PathTag {};
	struct QueryTag {};
	struct FormTag {};
	struct HeaderTag {};
	struct BodyTag {};

	// ---------- 值类型标签 ----------
	struct TypeString {};
	struct TypeQString {};
	struct TypeInt {};
	struct TypeFloat {};
	struct TypeBool {};
	struct TypeJson {};
	struct TypeFile {};
	struct TypeBinary {};

	template <typename T>
	struct TypeModel {};

	// ---------- FileValue：文件上传值 ----------
	struct FileValue {
		QString filePath;
		QByteArray data;
		QString fileName;
		QString contentType;

		static FileValue fromPath(const QString& path, const QString& contentType = {})
		{
			FileValue value;
			value.filePath = path;
			value.contentType = contentType;
			return value;
		}

		static FileValue fromBytes(const QByteArray& data,
			const QString& fileName,
			const QString& contentType = {})
		{
			FileValue value;
			value.data = data;
			value.fileName = fileName;
			value.contentType = contentType;
			return value;
		}
	};

	// ---------- ValueTypeOf：值类型标签 → C++ 类型映射 ----------
	template <typename T>
	struct ValueTypeOf;

	template <>
	struct ValueTypeOf<TypeString> { using type = std::string_view; };

	template <>
	struct ValueTypeOf<TypeQString> { using type = QString; };

	template <>
	struct ValueTypeOf<TypeInt> { using type = int64_t; };

	template <>
	struct ValueTypeOf<TypeFloat> { using type = double; };

	template <>
	struct ValueTypeOf<TypeBool> { using type = bool; };

	template <>
	struct ValueTypeOf<TypeJson> { using type = QJsonObject; };

	template <>
	struct ValueTypeOf<TypeFile> { using type = FileValue; };

	template <>
	struct ValueTypeOf<TypeBinary> { using type = QByteArray; };

	template <typename T>
	struct ValueTypeOf<TypeModel<T>> { using type = T; };

	// ---------- NoKeyStr：Body 等无键名参数的占位 KeyStr ----------
	struct NoKeyStr {
		static constexpr std::string_view view() { return ""; }
		static constexpr const char* c_str() { return ""; }
	};

	template <typename Tag, typename KeyStr, typename ValueType = TypeString>
	struct HttpRequestParam {
		using tag = Tag;
		using key = KeyStr;
		using value_tag = ValueType;
		using value_type = typename ValueTypeOf<ValueType>::type;

		static constexpr std::string_view key_view = KeyStr::view();
		static constexpr const char* key_cstr = KeyStr::c_str();
	};

	// ---------- 便捷别名 ----------
	template <typename KeyStr, typename ValueType = TypeString>
	using Path = HttpRequestParam<PathTag, KeyStr, ValueType>;

	template <typename KeyStr, typename ValueType = TypeString>
	using Query = HttpRequestParam<QueryTag, KeyStr, ValueType>;

	template <typename KeyStr, typename ValueType = TypeString>
	using Form = HttpRequestParam<FormTag, KeyStr, ValueType>;

	template <typename KeyStr, typename ValueType = TypeString>
	using Header = HttpRequestParam<HeaderTag, KeyStr, ValueType>;

	// Body 无键名，KeyStr 固定为 NoKeyStr 占位
	template <typename ValueType>
	using Body = HttpRequestParam<BodyTag, NoKeyStr, ValueType>;
}
