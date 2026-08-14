#pragma once

#include <string_view>

#include "HttpRequest.h"
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

}
