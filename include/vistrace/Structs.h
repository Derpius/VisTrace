#pragma once

#include <cstddef>

namespace VisTrace
{
	struct Pixel
	{
		float r = 0, g = 0, b = 0, a = 1;

		float& operator[](size_t i)
		{
			switch (i) {
			case 0:
				return r;
			case 1:
				return g;
			case 2:
				return b;
			case 3:
				return a;
			default:
				return r;
			}
		}
	};
}
