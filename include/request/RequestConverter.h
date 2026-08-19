#pragma once

#include <QString>
#include <cstdint>
#include <string_view>
#include <tuple>
#include <utility>

namespace NetCore {

	struct ValueConverter {
		static QString toString(std::string_view v)
		{
			return QString::fromUtf8(v.data(), static_cast<int>(v.size()));
		}

		static QString toString(int64_t v) { return QString::number(v); }

		static QString toString(double v) { return QString::number(v); }

		static QString toString(bool v) { return v ? "true" : "false"; }

		static QString toString(QString v) { return v; }
	};

	class RequestConverter {
	public:
		RequestConverter() = delete;
		~RequestConverter() = delete;

		template <typename ParamValueTuple, class Fn>
		static void forEachHttpRequestParam(ParamValueTuple&& param_values, Fn&& fn)
		{
			std::apply([&](auto &&...param_value) {
				(fn(param_value), ...);
				}, std::forward<ParamValueTuple>(param_values));
		}
	};

} // namespace NetCore
