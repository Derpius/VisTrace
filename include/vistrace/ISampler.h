#pragma once

namespace VisTrace
{
	class ISampler
	{
	public:
		ISampler() {};

		virtual float GetFloat() = 0;
		virtual void GetFloat2D(float& r1, float& r2) = 0;
	};
}
