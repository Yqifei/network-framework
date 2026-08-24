#pragma once

#include <QByteArray>
#include <QHash>
#include <QJsonObject>
#include <QString>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

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

	// ---------- 值类型分类 Trait ----------
	template <typename T>
	struct IsModelType : std::false_type {};

	template <typename T>
	struct IsModelType<TypeModel<T>> : std::true_type {};

	template <typename T>
	constexpr bool IsModelType_v = IsModelType<T>::value;

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

	// ---------- 参数分类 Trait ----------
	template <typename T>
	struct IsPathParam : std::false_type {};

	template <typename S, typename V>
	struct IsPathParam<Path<S, V>> : std::true_type {};

	template <typename T>
	struct IsQueryParam : std::false_type {};

	template <typename S, typename V>
	struct IsQueryParam<Query<S, V>> : std::true_type {};

	template <typename T>
	struct IsFormParam : std::false_type {};

	template <typename S, typename V>
	struct IsFormParam<Form<S, V>> : std::true_type {};

	template <typename T>
	struct IsHeaderParam : std::false_type {};

	template <typename S, typename V>
	struct IsHeaderParam<Header<S, V>> : std::true_type {};

	template <typename V>
	struct IsBodyParam : std::false_type {};

	template <typename V>
	struct IsBodyParam<Body<V>> : std::true_type {};

	// ----------编译期参数过滤----------
	template <typename Tag, typename Accumulated, typename... Params>
	struct FilterParamsImpl {
		using type = Accumulated;
	};

	template <typename Tag, typename... Acc, typename P, typename... Rest>
	struct FilterParamsImpl<Tag, std::tuple<Acc...>, P, Rest...> {
		using type = typename std::conditional_t<
			std::is_same_v<typename P::tag, Tag>,
			FilterParamsImpl<Tag, std::tuple<Acc..., P>, Rest...>,
			FilterParamsImpl<Tag, std::tuple<Acc...>, Rest...>
		>::type;
	};

	template <typename Tag, typename... Params>
	using FilterHttpRequestParams = FilterParamsImpl<Tag, std::tuple<>, Params...>;


	template <typename Param>
	struct HttpRequestParamValue {
		using value_type = typename Param::value_type;
		static constexpr std::string_view key = Param::key_view;
		value_type value{};
	};

	template <typename... Params>
	struct HttpRequestParamValueList {
		std::tuple<HttpRequestParamValue<Params>...> values;

		template <typename Param>
		void set(typename Param::value_type val)
		{
			std::get<HttpRequestParamValue<Param>>(values).value = std::move(val);
		}

		template <typename Param>
		const typename Param::value_type& get() const
		{
			return std::get<HttpRequestParamValue<Param>>(values).value;
		}
	};

	template <typename Tuple>
	struct MakeValueList {
		using type = HttpRequestParamValueList<>;
	};

	template <typename... Ps>
	struct MakeValueList<std::tuple<Ps...>> {
		using type = HttpRequestParamValueList<Ps...>;
	};

	namespace detail {

		template <typename Instance, typename Param, typename Value>
		void fillHttpRequestParamValue(Instance& inst, Value&& val)
		{
			using Tag = typename Param::tag;
			if constexpr (std::is_same_v<Tag, PathTag>) {
				inst.path.template set<Param>(std::forward<Value>(val));
			}
			else if constexpr (std::is_same_v<Tag, QueryTag>) {
				inst.query.template set<Param>(std::forward<Value>(val));
			}
			else if constexpr (std::is_same_v<Tag, FormTag>) {
				inst.form.template set<Param>(std::forward<Value>(val));
			}
			else if constexpr (std::is_same_v<Tag, HeaderTag>) {
				inst.headers.template set<Param>(std::forward<Value>(val));
			}
			else if constexpr (std::is_same_v<Tag, BodyTag>) {
				inst.body.template set<Param>(std::forward<Value>(val));
			}
		}

		template <typename Instance, typename ParamsTuple, typename ValuesTuple, size_t... Is>
		void fillAll(Instance& inst, ValuesTuple&& vals, std::index_sequence<Is...>)
		{
			(fillHttpRequestParamValue<Instance, std::tuple_element_t<Is, ParamsTuple>>(
				inst, std::get<Is>(std::forward<ValuesTuple>(vals))),
				...);
		}

	} // namespace detail

	template <typename T, typename = void>
	struct IsHttpRequest : std::false_type {};

	template <typename T>
	struct IsHttpRequest<T, std::void_t<decltype(T::method)>> : std::true_type {};

	template <typename T>
	constexpr bool is_http_request_v = IsHttpRequest<T>::value;

	template <typename RequestMeta,
		typename std::enable_if_t<is_http_request_v<RequestMeta>, int> = 0>
	struct HttpRequestInstance {
		typename MakeValueList<typename RequestMeta::path_params>::type path;
		typename MakeValueList<typename RequestMeta::query_params>::type query;
		typename MakeValueList<typename RequestMeta::form_params>::type form;
		typename MakeValueList<typename RequestMeta::header_params>::type headers;
		typename MakeValueList<typename RequestMeta::body_params>::type body;

		QHash<QByteArray, QByteArray> runtime_headers;
		int timeout_override_ms = -1;

		auto& withTimeout(int ms) { timeout_override_ms = ms; return *this; }
		auto& withNoTimeout() { timeout_override_ms = 0; return *this; }
	};

	template <HttpMethod Method, typename PathStr, typename ResponseType, typename... Params>
	struct HttpRequest {
		static constexpr HttpMethod method = Method;
		static constexpr std::string_view path_view = PathStr::view();
		static constexpr const char* path_cstr = PathStr::c_str();

		using response_type = ResponseType;
		using params_type = std::tuple<Params...>;

		using path_params = typename FilterHttpRequestParams<PathTag, Params...>::type;
		using query_params = typename FilterHttpRequestParams<QueryTag, Params...>::type;
		using form_params = typename FilterHttpRequestParams<FormTag, Params...>::type;
		using header_params = typename FilterHttpRequestParams<HeaderTag, Params...>::type;
		using body_params = typename FilterHttpRequestParams<BodyTag, Params...>::type;

		using instance_type = HttpRequestInstance<HttpRequest>;

		static_assert(!(std::tuple_size_v<body_params> > 0 && std::tuple_size_v<form_params> > 0),
			"Cannot use Body and Form params at the same time");
		static_assert(std::tuple_size_v<body_params> <= 1,
			"Only one Body param is allowed");

		static instance_type make(typename Params::value_type... args)
		{
			instance_type inst;
			detail::fillAll<instance_type, params_type>(
				inst,
				std::forward_as_tuple(std::move(args)...),
				std::index_sequence_for<Params...>{});
			return inst;
		}
	};

	template <HttpMethod Method, typename PathStr, typename ResponseType, typename... Params>
	struct HttpMultipartRequest : HttpRequest<Method, PathStr, ResponseType, Params...>
	{
		static_assert(Method == HttpMethod::POST || Method == HttpMethod::PUT || Method == HttpMethod::PATCH,
			"Multipart only support POST/PUT/PATCH");

		using base_t = HttpRequest<Method, PathStr, ResponseType, Params...>;
		using instance_type = HttpRequestInstance<HttpMultipartRequest>;

		static instance_type make(typename Params::value_type... args)
		{
			instance_type inst;
			detail::fillAll<instance_type, typename base_t::params_type>(
				inst,
				std::forward_as_tuple(std::move(args)...),
				std::index_sequence_for<Params...>{});
			return inst;
		}
	};

	template <typename T, typename = void>
	struct is_multipart_request : std::false_type {};

	template <HttpMethod M, typename PathStr, typename ResponseType, typename... Params>
	struct is_multipart_request<HttpMultipartRequest<M, PathStr, ResponseType, Params...>> : std::true_type {};

	template <typename T>
	constexpr bool is_multipart_request_v = is_multipart_request<T>::value;


}
