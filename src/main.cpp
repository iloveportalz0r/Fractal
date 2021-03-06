#include <algorithm>
#include <chrono>
#include <cmath>
#include <complex>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <stdint.h>
#include <string>
#include <type_traits>
#include <vector>

#include <signal.h>

#include <png++/png.hpp>

#include "ArgParser.hpp"
#include "kompleks.hpp"

using std_clock = std::chrono::steady_clock;
using std_duration = std_clock::time_point::duration;
using std::string;

using int128_t = __int128;
using uint128_t = unsigned __int128;

const int128_t INT128_MAX = static_cast<int128_t>((uint128_t(1) << ((__SIZEOF_INT128__ * __CHAR_BIT__) - 1)) - 1);
constexpr kompleks_type INF = __builtin_infl();

// std::chrono::nanoseconds::rep is signed
static uint64_t to_ns(const std_duration& d)
{
	return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(d).count());
}

#define FRACTAL_TYPE \
	X(mandelbrot, "mandelbrot") \
	X(julia, "julia") \
	X(burning_ship, "burning ship") \
	X(tricorn, "tricorn") \
	X(neuron, "neuron") \
	X(clouds, "clouds") \
	X(oops, "oops") \
	X(stupidbrot, "stupidbrot") \
	X(untitled1, "untitled 1") \
	X(dots, "dots") \
	X(magnet1, "magnet 1") \
	X(experiment, "experiment") \
	X(mandelbox, "mandelbox") \
	X(negamandelbrot, "negamandelbrot") \
	X(collatz, "collatz") \
	X(experiment2, "experiment2")

#define X(a, b) a,
enum class FractalType : uint8_t
{
	FRACTAL_TYPE
};
#undef X

#define X(a, b) b,
static const string FractalType_strings[]
{
	FRACTAL_TYPE
};
#undef X
constexpr size_t FractalType_count = sizeof(FractalType_strings) / sizeof(FractalType_strings[0]);

static FractalType string_to_fractal_type(const string& typestr)
{
	for(size_t i = 0; i < FractalType_count; ++i)
	{
		if(typestr == FractalType_strings[i])
		{
			return static_cast<FractalType>(i);
		}
	}

	throw std::runtime_error("Unknown fractal type: " + string(typestr));
}
static std::ostream& operator<<(std::ostream& o, const FractalType t)
{
	using T = std::underlying_type_t<FractalType>;
	T tu = static_cast<T>(t);
	if(tu >= FractalType_count)
	{
		return o << std::to_string(tu);
	}
	return o << FractalType_strings[tu];
}

static struct FractalOptions
{
	static FractalType type;
	static kompleks_type exponent;
	static kompleks_type escape_limit;
	static bool single;
	static kompleks_type lbound;
	static kompleks_type rbound;
	static kompleks_type bbound;
	static kompleks_type ubound;
	static kompleks_type juliaA;
	static kompleks_type juliaB;
} fractal_opt;
FractalType FractalOptions::type = FractalType::mandelbrot;
kompleks_type FractalOptions::exponent = 2;
kompleks_type FractalOptions::escape_limit = 4;
bool FractalOptions::single = false;
kompleks_type FractalOptions::lbound = 2;
kompleks_type FractalOptions::rbound = 2;
kompleks_type FractalOptions::bbound = 2;
kompleks_type FractalOptions::ubound = 2;
kompleks_type FractalOptions::juliaA = -0.8L;
kompleks_type FractalOptions::juliaB = 0.156L;

static struct ColorOptions
{
	static uint_fast16_t method;
	static bool smooth;
	static bool disable_fancy;
	static kompleks_type multiplier;
	static unsigned int c_log;
} color_opt;
uint_fast16_t ColorOptions::method = 0;
bool ColorOptions::smooth = false;
bool ColorOptions::disable_fancy = false;
kompleks_type ColorOptions::multiplier = 1;
unsigned int ColorOptions::c_log = 0;

// https://github.com/kobalicek/rgbhsv/blob/master/src/rgbhsv.cpp
static void HSV2RGB
(
	kompleks_type h,
	const kompleks_type s,
	kompleks_type v,
	uint_fast8_t* dst
)
{
	// TODO: use mod
	while(h < 0)
	{
		h += 1;
	}
	while(h >= 1)
	{
		h -= 1;
	}
	h *= 6;

	uint_fast8_t index = static_cast<uint_fast8_t>(h);
	kompleks_type f = h - static_cast<kompleks_type>(index);
	kompleks_type p = (v * (1 - s)) * 255;
	kompleks_type q = (v * (1 - s * f)) * 255;
	kompleks_type t = (v * (1 - s * (1 - f))) * 255;
	v *= 255;

	switch(index)
	{
		#define kast(x) static_cast<uint_fast8_t>(x)
		case 0: dst[0] = kast(v); dst[1] = kast(t); dst[2] = kast(p); break;
		case 1: dst[0] = kast(q); dst[1] = kast(v); dst[2] = kast(p); break;
		case 2: dst[0] = kast(p); dst[1] = kast(v); dst[2] = kast(t); break;
		case 3: dst[0] = kast(p); dst[1] = kast(q); dst[2] = kast(v); break;
		case 4: dst[0] = kast(t); dst[1] = kast(p); dst[2] = kast(v); break;
		case 5: dst[0] = kast(v); dst[1] = kast(p); dst[2] = kast(q); break;
		#undef kast
		default:
		{
			// should never happen
			throw std::runtime_error("index must be in range [0, 5], but is " + std::to_string(index));
		}
	}
}

static const png::rgb_pixel colorize
(
	const uint_fast32_t color_method,
	const kompleks& Z,
	const kompleks& c,
	const uint_fast64_t n
)
{
	kompleks_type red, green, blue;
	const kompleks_type Zr2 = Z.real*Z.real;
	const kompleks_type Zi2 = Z.imag*Z.imag;
	switch(color_method)
	{
		case 0: // escape time (gold)
		{
			if(color_opt.smooth)
			{
				// from http://www.hpdz.net/TechInfo/Colorizing.htm#FractionalCounts
				kompleks_type dx = (std::log(std::log(fractal_opt.escape_limit)) - std::log(std::log(Z.abs()))) / std::log(fractal_opt.exponent);
				kompleks_type nprime = n + dx;
				red = std::round(nprime * 2);
				green = std::round(nprime);
				blue = std::round(nprime / 2);
			}
			else
			{
				red = n << 1;
				green = n;
				blue = n >> 1;
			}

			break;
		}
		case 1: // escape time (green + some shit)
		{
			if(!color_opt.disable_fancy)
			{
				red = Zr2;
				blue = Zi2;
			}
			else
			{
				red = 0;
				blue = 0;
			}
			if(color_opt.smooth)
			{
				kompleks_type dx = (std::log(std::log(fractal_opt.escape_limit)) - std::log(std::log(Z.abs()))) / std::log(fractal_opt.exponent);
				green = std::round(n + dx);
			}
			else
			{
				green = n;
			}
			if(green > 255)
			{
				kompleks_type difference = green - 255;
				green = 255;
				blue = difference * 2;
				if(blue > 255)
				{
					red = blue * 2;
					blue = 200;
					green = 200;
				}
			}
			break;
		}
		case 2: // lazer shit 1
		{
			red = Zr2 * Zi2;
			green = Zr2 + Zi2;
			if(Zi2 == 0)
			{
				blue = INF;
			}
			else
			{
				blue = Zr2 / Zi2;
			}
			break;
		}
		case 3: // lazer shit 2
		{
			if(Zr2 == 0)
			{
				red = INF;
				green = INF;
			}
			else
			{
				red = (Zr2 * Zr2 * Zr2 + 1) / Zr2;
				green = Zi2 / Zr2;
			}
			blue = Zi2 * Zi2;
			break;
		}
		case 4: // Ben
		{
			red = green = blue = Z.real * std::sin(Z.imag + Zi2) - Zr2;
			break;
		}
		case 5: // Glow (Green)
		{
			if(Zr2 <= (1.0L / UINT_FAST64_MAX))
			{
				red = UINT_FAST64_MAX;
			}
			else
			{
				red = std::round(1 / Zr2);
			}
			if(Zr2 <= 0.00588L)
			{
				green = UINT_FAST64_MAX;
			}
			else
			{
				green = std::round(1.5L / Zr2);
			}
			if(Zr2 <= 0.00294L)
			{
				blue = UINT_FAST64_MAX;
			}
			else
			{
				blue = std::round(0.75L / Zr2);
			}
			break;
		}
		case 6: // Glow (Pink)
		{
			if(Zr2 == 0)
			{
				red = UINT_FAST64_MAX;
			}
			else
			{
				red = std::round(1.5L / Zr2);
			}
			if(Zr2 == 0)
			{
				green = UINT_FAST64_MAX;
			}
			else
			{
				green = std::round(0.75L / Zr2);
			}
			if(Zr2 == 0)
			{
				blue = UINT_FAST64_MAX;
			}
			else
			{
				blue = std::round(1 / Zr2);
			}
			break;
		}
		case 7: // Glow (Blue)
		{
			if(Zr2 <= 0.00294L)
			{
				red = UINT_FAST64_MAX;
			}
			else
			{
				red = std::round(0.75L / Zr2);
			}
			if(Zr2 <= 0.00392L)
			{
				green = UINT_FAST64_MAX;
			}
			else
			{
				green = std::round(1 / Zr2);
			}
			if(Zr2 <= 0.00588L)
			{
				blue = UINT_FAST64_MAX;
			}
			else
			{
				blue = std::round(1.5L / Zr2);
			}
			break;
		}
		case 8: // Bright pink with XOR
		{
			if(Zr2 == 0)
			{
				red = INF;
			}
			else
			{
				red = Zi2 / Zr2 + (n << 1);
			}
			if(Zi2 == 0)
			{
				green = INF;
			}
			else
			{
				green = Zr2 / Zi2 + n;
			}
			// TODO: stop using integers here
			if(Zi2 > INT128_MAX / 255 || Zr2 > INT128_MAX / 255)
			{
				blue = INT128_MAX;
			}
			else
			{
				blue = static_cast<int128_t>(std::round(Zi2 * 255)) ^ static_cast<int128_t>(std::round(Zr2 * 255));
			}
			red += blue * 0.5L;
			green += blue * 0.2L;

			red *= 0.1L;
			green *= 0.1L;
			blue *= 0.1L;

			break;
		}
		case 9:
		{
			png::rgb_pixel color_fractal = colorize(0, Z, c, n);
			uint_fast64_t red_fractal = color_fractal.red,
						  green_fractal = color_fractal.green,
						  blue_fractal = color_fractal.blue;

			red = static_cast<uint_fast64_t>(std::round(Zr2*8)) ^ static_cast<uint_fast64_t>(std::round(Zi2*8));
			green = static_cast<uint_fast64_t>(std::round(Zr2*2)) ^ static_cast<uint_fast64_t>(std::round(Zi2*2));
			blue = static_cast<uint_fast64_t>(std::round(Zr2*4)) ^ static_cast<uint_fast64_t>(std::round(Zi2*4));

			// darken the colors a bit
			red *= 0.7L;
			green *= 0.7L;
			blue *= 0.7L;

			uint_fast64_t blue_stripe;
			if(Zr2 == 0)
			{
				blue_stripe = 255;
			}
			else
			{
				blue_stripe = static_cast<uint_fast64_t>(std::round(Zi2 / Zr2));
			}
			uint_fast64_t green_stripe;
			if(Zi2 == 0)
			{
				green_stripe = 255;
			}
			else
			{
				green_stripe = static_cast<uint_fast64_t>(std::round(Zr2 / Zi2));
			}
			green_stripe += blue_stripe;

			/*if(red > 255) red = red % 255;
			if(green > 255) green = green % 255;
			if(blue > 255) blue = blue % 255;
			if(green_stripe > 255) green_stripe = green_stripe % 255;
			if(blue_stripe > 255) blue_stripe = blue_stripe % 255;*/

			red *= color_opt.multiplier;
			green *= color_opt.multiplier;
			blue *= color_opt.multiplier;

			if(red > 255) red = 255;
			if(green > 255) green = 255;
			if(blue > 255) blue = 255;
			if(green_stripe > 255) green_stripe = 255;
			if(blue_stripe > 255) blue_stripe = 255;

			red -= (blue_stripe > red ? red : blue_stripe);
			red -= (green_stripe > red ? red : green_stripe);
			green -= (blue_stripe > green ? green : blue_stripe);
			green -= (green_stripe > green ? green : green_stripe);
			blue -= (blue_stripe > blue ? blue : blue_stripe);
			blue -= (green_stripe > blue ? blue : green_stripe);

			uint_fast64_t sub = red_fractal + green_fractal + blue_fractal;
			red -= (sub > red ? red : sub);
			green_stripe -= (sub > green_stripe ? green_stripe : sub);
			blue_stripe -= (sub > blue_stripe ? blue_stripe : sub);

			red += red_fractal;
			green += green_stripe + green_fractal;
			blue += blue_stripe + blue_fractal;
			break;
		}
		case 10:
		{
			red = (n << 1) ^ n;
			green = (n);
			blue = (n >> 1) ^ n;
			break;
		}
		case 11:
		{
			red = Zr2;
			green = Zr2 * Zi2;
			blue = Zi2;
			break;
		}
		case 12: // binary
		{
			red = green = blue = 255;
			break;
		}
		case 13: // purple (escape time)
		{
			red = (n << 2) + 5;
			green = (n << 1) + 1;
			blue = (n << 2) + 2;
			break;
		}
		case 14: // random; todo
		{
			// TODO: use C++ random
			srand(static_cast<unsigned int>(n));
			red = rand() & 0xFF;
			green = rand() & 0xFF;
			blue = rand() & 0xFF;
			break;
		}
		case 15: // hue
		{
			uint_fast8_t colors[3];
			HSV2RGB((n % 256) / 256.0L, 1, 1, colors);
			red = colors[0];
			green = colors[1];
			blue = colors[2];
			break;
		}
		case 16:
		{
			red = n * n * 0.1L;
			green = n;
			blue = Zr2 * Zi2;
			break;
		}
		case 17:
		{
			kompleks_type r = 2 * std::sin(Zr2);
			kompleks_type g = 2 * std::cos(Zi2);
			kompleks_type b = r * g;
			red = r * 127;
			green = g * 127;
			blue = b * 127;
			break;
		}
		default:
		{
			throw std::runtime_error("Invalid color method: " + std::to_string(color_method));
		}
	}

	for(unsigned int i = 0; i < color_opt.c_log; ++i)
	{
		red = std::log(red);
		green = std::log(green);
		blue = std::log(blue);
	}

	if(color_method != 9)
	{
		/*
		if(color_opt.multiplier > 1)
		{
			uint_fast64_t max = UINT_FAST64_MAX / color_opt.multiplier; // prevent overflow
			red = red > max ? UINT_FAST64_MAX : red * color_opt.multiplier;
			green = green > max ? UINT_FAST64_MAX : green * color_opt.multiplier;
			blue = blue > max ? UINT_FAST64_MAX : blue * color_opt.multiplier;
		}
		else
		*/
		{
			red *= color_opt.multiplier;
			green *= color_opt.multiplier;
			blue *= color_opt.multiplier;
		}
	}

	if(red > 255)
	{
		red = 255;
	}
	else if(red < 0)
	{
		red = 0;
	}

	if(green > 255)
	{
		green = 255;
	}
	else if(green < 0)
	{
		green = 0;
	}

	if(blue > 255)
	{
		blue = 255;
	}
	else if(blue < 0)
	{
		blue = 0;
	}

	const uint8_t r = static_cast<uint8_t>(std::round(red));
	const uint8_t g = static_cast<uint8_t>(std::round(green));
	const uint8_t b = static_cast<uint8_t>(std::round(blue));

	return png::rgb_pixel(r, g, b);
}

static kompleks iterate
(
	kompleks Z,
	kompleks& c,
	const uint_fast64_t n
)
{
	switch(fractal_opt.type)
	{
		case FractalType::mandelbrot:
		case FractalType::julia:
		{
			return (Z^fractal_opt.exponent) + c;
		}
		case FractalType::burning_ship:
		{
			kompleks_type real_abs = std::abs(Z.real);
			kompleks_type imag_abs = std::abs(Z.imag);
			return (kompleks(real_abs, imag_abs)^fractal_opt.exponent) + c;
		}
		case FractalType::tricorn:
		{
			// this formula shows it flipped horizontally
			//return (Z.swap_xy()^fractal_opt.exponent) + c;

			// this is the formula given on Wikipedia
			return (Z.conjugate()^fractal_opt.exponent) + c;
		}
		case FractalType::neuron:
		{
			// original flipped formula; higher exponents are rotated slightly
			return (Z.swap_xy()^fractal_opt.exponent) + Z;

			// this formula matches the tricorn; use this to get unrotated images
			//return (Z.conjugate()^fractal_opt.exponent) + Z;
		}
		case FractalType::clouds:
		case FractalType::oops:
		{
			kompleks new_z = (Z.swap_xy()^fractal_opt.exponent) + c;
			c = Z;
			return new_z;
		}
		case FractalType::stupidbrot:
		{
			Z = (Z^fractal_opt.exponent);
			if(n % 2 == 0)
			{
				Z = Z + c;
			}
			else
			{
				Z = Z - c;
			}
			return Z;
		}
		case FractalType::untitled1:
		{
			std::complex<kompleks_type> Z_std = pow(Z.to_std(), Z.to_std());
			return kompleks(Z_std) + Z;
		}
		case FractalType::dots:
		{
			return (Z^fractal_opt.exponent) * c.reciprocal(); // equivalent to & faster than: (Z^fractal_opt.exponent) / c
		}
		case FractalType::magnet1:
		{
			return (((Z^2) + (c - 1)) / (Z * 2 + (c - 2))) ^ 2;
		}
		case FractalType::experiment:
		{
			//return lepow(c, fractal_opt.exponent) + Z;

			// diagonal line
			//return Z.swap_xy() + c;

			//return (Z^(fractal_opt.exponent + 1)) + (Z^fractal_opt.exponent) + c;
			return (Z^fractal_opt.exponent) + c.reciprocal();
		}
		case FractalType::mandelbox:
		{
			auto boxfold = [](kompleks_type component)
			{
				if(component > 1)
				{
					return 2 - component;
				}
				if(component < -1)
				{
					return -2 - component;
				}
				return component;
			};
			Z.real = boxfold(Z.real);
			Z.imag = boxfold(Z.imag);

			if(Z.abs() < 0.5L)
			{
				Z = Z / 0.25L; // 0.5*0.5
			}
			else if(Z.abs() < 1)
			{
				Z = Z / Z.norm();
			}

			return fractal_opt.exponent * Z + c;
		}
		case FractalType::negamandelbrot:
		{
			return (Z^(1 / fractal_opt.exponent)) - c;
		}
		case FractalType::collatz:
		{
			return (2.0L + 7.0L * Z - (2.0L + 5.0L * Z) * cos(M_PIl * Z)) / 4.0L;
		}
		case FractalType::experiment2:
		{
			//return kompleks(pow(Z.real, fractal_opt.exponent), pow(Z.imag, fractal_opt.exponent)) + c;
			return (Z^fractal_opt.exponent) + (c^(1/fractal_opt.exponent));
		}
		default:
		{
			std::ostringstream ss;
			ss << "Unhandled fractal type in iterate: " << fractal_opt.type;
			throw std::runtime_error(ss.str());
		}
	}
}

static bool can_skip(const kompleks_type x, const kompleks_type y)
{
	if(fractal_opt.single
	|| fractal_opt.type != FractalType::mandelbrot
	|| fractal_opt.escape_limit != 4)
	{
		return false;
	}

	if(fractal_opt.exponent == 2)
	{
		const kompleks_type y2 = y*y;
		const kompleks_type xo = x - 0.25L;
		const kompleks_type q = xo*xo + y2;
		return (q * (q + xo) < 0.25L * y2   // p1 cardioid
		    || (x+1)*(x+1) + y2 < 0.0625L); // p2 bulb
	}

	/*
	See: http://cosinekitty.com/mandel_orbits_analysis.html
	It has:
		c = z - z^2
		(∂/∂z) (z^2 + c) = e^(i*θ)
		2z = e^(i*θ)
		z = (e^(i*θ)) / 2
		c = ((e^(i*θ)) / 2) - ((e^(i*θ)) / 2)^2

	If the exponent is 3:
		z^3 + c = z
		c = z - z^3
		(∂/∂z) (z^3 + c) = e^(i*θ)
		3*z^2 = e^(i*θ)
	I used Mathematica to solve for c and separate its components. As a parametric equation:
		x(t) = (3*cos(t/2) - cos(3*t/2)) / (3*sqrt(3))
		y(t) = ±((4*sin(t/2)^3) / (3*sqrt(3)))
	For some y value, I want the corresponding x value, so I solved for t and got inverse y:
		t(y) = 2*arcsin(cuberoot(3*sqrt(3)/4 * y))
	Then I used Mathematica to help simplify:
		x(t(y)) = ±(sqrt(4/3 - a) * (3a + 2))/6
		where a = cuberoot(2*y)^2
	Then I squared it and simplified
	*/
	if(fractal_opt.exponent == 3)
	{
		/* ellipse method that gets some (not all!) points
		const kompleks_type a = 0.384900179459750509673; // x(0)
		const kompleks_type b = 0.769800358919501019346; // y(tau/2)
		return (x*x)/(a*a) + (y*y)/(b*b) < 1;*/

		/* I was tired when I did this
		kompleks_type a = pow(2 * y, 1.0 / 3.0); a *= a;
		kompleks_type b = sqrt(4.0 / 3.0 - a) * (3*a + 2) / 6.0;
		return x < b && x > -b;*/

		kompleks_type y2 = y*y;
		if(x*x < 4.0L/27.0L - y2 + std::pow(4 * y2, 1.0L / 3.0L)/3.0L)
		{
			return true;
		}
	}

	/*
	If the exponent is 4:
		z^4 + c = z
		c = z - z^4
		(∂/∂z) (z^4 + c) = e^(i*θ)
		4*z^3 = e^(i*θ)

	*/
	if(fractal_opt.exponent == 4)
	{
		// partial capture: circle with radius (9 / (32 * 2^(1/3)))
		// see https://www.desmos.com/calculator/qdeni0ojwu
		return (x*x + y*y < 0.2232282729330280511369586055226683491L);
	}

	if(fractal_opt.exponent == 5)
	{
		// partial capture: circle with radius (16 / 5^2.5)
		// see https://www.desmos.com/calculator/dagfi9vchf
		return (x*x + y*y < 0.2862167011199730811403742295976033581L);
	}

	return false;
}

static uint32_t width_px = 512;
static uint32_t height_px = 512;
static uint_fast64_t max_iterations = 1024;
static uint_fast32_t pCheckN = 1; // periodicity checking
static volatile sig_atomic_t cancel = false;

static string make_filename
(
	uint_fast64_t max_n,
	uint_fast64_t max_period_n,
	uint_fast64_t not_escaped
)
{
	std::ostringstream ss;
	ss << "tiles/" << fractal_opt.type << '/' << color_opt.method << '/';

	if(fractal_opt.single)
	{
		ss << "single_";
	}
	//if(fractal_opt.type != collatz) // still leaves an underscore at the beginning
	{
		ss << "e" << fractal_opt.exponent;
	}

	if(fractal_opt.lbound != -2)
	{
		ss << "_lb" << fractal_opt.lbound;
	}
	if(fractal_opt.rbound != 2)
	{
		ss << "_rb" << fractal_opt.rbound;
	}
	if(fractal_opt.bbound != -2)
	{
		ss << "_bb" << fractal_opt.bbound;
	}
	if(fractal_opt.ubound != 2)
	{
		ss << "_ub" << fractal_opt.ubound;
	}

	if(fractal_opt.type == FractalType::julia)
	{
		ss << "_jx" << fractal_opt.juliaA << "_jy" << fractal_opt.juliaB;
	}
	if(color_opt.method == 1 && color_opt.disable_fancy)
	{
		ss << "_df";
	}

	if(!fractal_opt.single)
	{
		ss << "_el" << fractal_opt.escape_limit;
	}
	ss << "_mi" << (fractal_opt.single ? max_iterations : max_n);
	ss << "_mpi" << max_period_n;

	if((color_opt.method == 0 || color_opt.method == 1) && color_opt.smooth)
	{
		ss << "_smooth";
	}
	ss << '_' << width_px << 'x';
	if(width_px != height_px)
	{
		ss << height_px;
	}
	if(color_opt.multiplier != 1)
	{
		ss << "_cm" << color_opt.multiplier;
	}
	if(color_opt.c_log != 0)
	{
		ss << "_clog" << color_opt.c_log;
	}
	if(cancel)
	{
		ss << "_partial";
	}
	else if(not_escaped == 0
	     && !fractal_opt.single)
	{
		ss << "_complete";
	}
	ss << "_ld"; // long double
	ss << ".png";
	return ss.str();
}

static size_t print_progress(const size_t prev_spaces, const string& startString, uint_fast64_t currentPoint, uint_fast64_t totalPoints)
{
	double percent = static_cast<double>(currentPoint) * 100.0 / totalPoints;
	std::ostringstream ss;
	ss.precision(3);
	ss << startString << " point " << currentPoint << " of " << totalPoints << " (" << percent << ")%";
	const string status = ss.str();
	size_t spaces = status.size();
	std::cout << '\r' << status;
	if(prev_spaces > spaces)
	{
		std::cout << string(prev_spaces - spaces, ' ');
		spaces = prev_spaces;
	}
	std::cout << std::flush;
	return spaces;
}

static void createFractal()
{
	const kompleks_type width = (fractal_opt.rbound - fractal_opt.lbound);
	const kompleks_type height = (fractal_opt.ubound - fractal_opt.bbound);
	const kompleks_type xinterval = width / width_px;
	const kompleks_type yinterval = height / height_px;

	const uint_fast64_t totalPoints = width_px * height_px;
	uint_fast64_t currentPoint = 0;

	uint_fast64_t periodic = 0; // amount of periodic points
	uint_fast64_t escaped = 0; // amount of escaped points
	uint_fast64_t not_escaped = 0; // amount of points that did not escape
	uint_fast64_t skipped = 0;
	uint_fast64_t run = 0; // amount of iterations processed
	uint_fast64_t max_n = 0; // maximum iterations used on a point that escaped
	uint_fast64_t max_period = 0;
	uint_fast64_t max_period_n = 0;

	std::ostringstream ss;
	ss << "Rendering " << fractal_opt.type << "...";
	string startString = ss.str();
	std::cout << startString << std::flush;
	size_t spaces = 0;

	const auto time_start = std_clock::now();
	auto previous_time = time_start;

	png::image<png::rgb_pixel> image(width_px, height_px);

	std::vector<kompleks> pCheck(pCheckN);
	kompleks c;

	for(uint_fast32_t pY = 0; pY < height_px; ++pY)
	{
		for(uint_fast32_t pX = 0; pX < width_px; ++pX)
		{
			using std::literals::chrono_literals::operator""s;
			const auto current_time = std_clock::now();
			if(current_time - previous_time >= 1s)
			{
				spaces = print_progress(spaces, startString, currentPoint, totalPoints);
				previous_time = current_time;
			}

			kompleks_type x = fractal_opt.lbound + pX * xinterval + xinterval / 2;
			kompleks_type y = fractal_opt.ubound - pY * yinterval - yinterval / 2;

			if(can_skip(x, y))
			{
				++skipped;
				//image.set_pixel(pX, pY, png::rgb_pixel(0, 255, 0));
			}
			else
			{
				kompleks Z;
				if(fractal_opt.type != FractalType::clouds
				&& fractal_opt.type != FractalType::mandelbrot
				)
				{
					Z.real = x;
					Z.imag = y;
				}

				if(fractal_opt.type == FractalType::julia)
				{
					c = kompleks(fractal_opt.juliaA, fractal_opt.juliaB);
				}
				else
				{
					c = kompleks(x, y);
				}

				std::fill(pCheck.begin(), pCheck.end(), Z);

				for(uint_fast64_t n = 0; n <= max_iterations; ++n)
				{
					++run;
					if((fractal_opt.single && n == max_iterations)
					|| (!fractal_opt.single && Z.norm() > fractal_opt.escape_limit && n > 0))
					{
						++escaped;
						if(n > max_n)
						{
							max_n = n;
						}
						image.set_pixel(pX, pY, colorize(color_opt.method, Z, c, n));
						break;
					}
					if(n == max_iterations)
					{
						++not_escaped;
						//image.set_pixel(pX, pY, png::rgb_pixel(255, 0, 0));
						break;
					}

					Z = iterate(Z, c, n);

					if(!fractal_opt.single && pCheckN > 0)
					{
						// if Z has had its current value in a previous iteration, stop iterating
						const auto location = std::find(pCheck.cbegin(), pCheck.cend(), Z);
						if(location != pCheck.cend())
						{
							size_t pCheckIndex = static_cast<size_t>(pCheck.cend() - location);
							if(pCheckIndex > max_period)
							{
								max_period = pCheckIndex;
							}
							if(n > max_period_n)
							{
								max_period_n = n;
							}
							++periodic;
							/*if(fractal_opt.type == neuron && (color_opt.method == 0 || color_opt.method == 1 || color_opt.method == 9))
							{
								image.set_pixel(pX, pY, png::rgb_pixel(255, 255, 255));
							}*/
							//image.set_pixel(pX, pY, png::rgb_pixel(255, 255, 255));
							//image.set_pixel(pX, pY, colorize(color_opt.method, Z, c, UINT64_MAX));
							goto end_iteration; // double break
						}

						// TODO: this is a fucking retarded slow method
						pCheck.erase(pCheck.begin());
						pCheck.emplace_back(Z);
					}
					if(cancel) // pressed CTRL+C
					{
						break;
					}
				}
				end_iteration:;
			}
			if(cancel) // pressed CTRL+C
			{
				break;
			}
			++currentPoint;
		}
	}

	const auto duration = std_clock::now() - time_start;
	const double duration_s = to_ns(duration) / 1e9;

	// the final line should be long enough to cover the status

	const string filename = make_filename(max_n, max_period_n, not_escaped);
	std::cout << '\r' << startString;
	std::cout << " done in " << duration_s << " second";
	if(duration_s != 1)
	{
		std::cout << 's';
	}
	std::cout << " ("
	          << escaped << " e, "
	          << not_escaped << " ne, "
	          << periodic << " p, "
	          << max_period << " mp, "
	          << max_period_n << " mpi, "
	          << skipped << " s, "
	          << run << " i, "
	          << max_n << " mi, "
	          << currentPoint << " t)\n";
	if(escaped + not_escaped + periodic + skipped != currentPoint)
	{
		std::cout << "There is a bug somewhere (e + ne + p + s != total)\n";
	}

	std::cout << "Saving " << filename << "..." << std::flush;
	image.write(filename);
	std::cout << " done\n";
}

static void show_help()
{
	std::cout << "[s] means string, [f] means float, and [i] means integer. Options that take a value will fail without one.\n";
	std::cout << " -s             Smooth the color bands for methods 0 and 1\n";
	std::cout << " -S             Color all points with the specified iteration count\n";
	std::cout << "                 instead of the escape time\n";
	std::cout << " -t         [s] Fractal type:\n";
	for(const auto& name : FractalType_strings)
	{
	std::cout << "                 " << name << '\n';
	}
	std::cout << " -jx        [f] The real part of c (for julia only)\n";
	std::cout << " -jy        [f] The imaginary part of c (for julia only)\n";
	std::cout << " -c         [i] The coloring method to use (default = 0):\n";
	std::cout << " -colors        List coloring methods\n";
	std::cout << " -df            Disable fancy coloring for method 1\n";
	std::cout << " -cm        [f] Color multiplier\n";
	std::cout << " -clog      [i] logarithm the colors\n";
	std::cout << " -r         [i] Picture size (width and height)\n";
	std::cout << " -i         [i] Maximum iterations for each point\n";
	std::cout << " -e         [f] Exponent (default = 2); higher absolute value = slower\n";
	std::cout << " -el        [f] Escape limit (default = 4)\n";
	std::cout << '\n';
	std::cout << "If an invalid value is specified, the default will be used. For the filters, the value you specify is how many iterations are run before the filter starts checking points.\n";
}

static void show_colors()
{
	std::cout << "Coloring methods:\n";
	std::cout << "    0 - gold (escape time)\n";
	std::cout << "    1 - green (escape time) with red/blue crap\n";
	std::cout << "    2 - green/orange crap with blue laser things\n";
	std::cout << "    3 - red/blue crap with green laser thingies\n";
	std::cout << "    4 - weird white and black crap\n";
	std::cout << "    5 - glowing (green)\n";
	std::cout << "    6 - glowing (pink)\n";
	std::cout << "    7 - glowing (blue)\n";
	std::cout << "    8 - pinkish XOR (might need -cm)\n";
	std::cout << "    9 - weird XOR stuff with lots of stripes\n";
	std::cout << "   10 - ugly pink thing\n";
	std::cout << "   11 - ugly green thing\n";
	std::cout << "   12 - black (set) and white (background)\n";
	std::cout << "   13 - purple (escape time)\n";
	std::cout << "   14 - random (escape time)\n";
	std::cout << "   15 - hue (escape time)\n";
	std::cout << "   16 - oversaturated orange/yellow (escape time) with blue crap\n";
}

int main(const int argc, char** const argv)
{
	if(argc < 2)
	{
		show_help();
		return 0;
	}
	const string firstArg(argv[1]);
	if(firstArg == "--help"
	|| firstArg == "-help"
	|| firstArg == "-h"
	|| firstArg == "-?")
	{
		show_help();
		return 0;
	}
	if(firstArg == "-colors")
	{
		show_colors();
		return 0;
	}

	ArgParser argp;
	argp.add("-df", false);
	argp.add("-s" , false);
	argp.add("-S" , false);

	argp.add("-c"     ,    0);
	argp.add("-cm"    ,    1.0L);
	argp.add("-clog"  ,    0);
	argp.add("-e"     ,    2.0L);
	argp.add("-el"    ,    4.0L);
	argp.add("-i"     , 1024);
	argp.add("-jx"    ,   -0.8L);
	argp.add("-jy"    ,    0.156L);
	argp.add("-pc"    ,    1);
	argp.add("-r"     , 1024);
	argp.add("-t"     , "mandelbrot");
	argp.add("-lbound",   -2.0L);
	argp.add("-rbound",    2.0L);
	argp.add("-bbound",   -2.0L);
	argp.add("-ubound",    2.0L);
	argp.add("-box"   ,    2.0L);
	argp.add("-wm"    ,    1.0L); // width multiplier

	try
	{
		argp.parse(argc, argv);
	}
	catch(const std::runtime_error& e)
	{
		std::cerr << e.what() << '\n';
		return 1;
	}

	color_opt.disable_fancy  = argp.get_bool("-df");
	color_opt.smooth         = argp.get_bool("-s");
	fractal_opt.single       = argp.get_bool("-S");

	color_opt.method         = argp.get_uint("-c");
	color_opt.multiplier     = argp.get_lfloat("-cm");
	color_opt.c_log          = argp.get_uint("-clog");

	fractal_opt.exponent     = argp.get_lfloat("-e");
	fractal_opt.escape_limit = argp.get_lfloat("-el");
	max_iterations           = argp.get_uint("-i");
	fractal_opt.juliaA       = argp.get_lfloat("-jx");
	fractal_opt.juliaB       = argp.get_lfloat("-jy");
	pCheckN                  = argp.get_uint("-pc");
	height_px                = argp.get_uint("-r");
	width_px                 = static_cast<uint32_t>(std::round(height_px * argp.get_lfloat("-wm")));
	try
	{
		fractal_opt.type = string_to_fractal_type(argp.get_string("-t"));
	}
	catch(std::runtime_error& e)
	{
		std::cerr << e.what() << '\n';
		return 1;
	}

	kompleks_type lbound, rbound, bbound, ubound;
	if(argp.get_lfloat("-box") != 2)
	{
		rbound = ubound = argp.get_lfloat("-box");
		lbound = bbound = -rbound;
	}
	else
	{
		lbound = argp.get_lfloat("-lbound");
		rbound = argp.get_lfloat("-rbound");
		bbound = argp.get_lfloat("-bbound");
		ubound = argp.get_lfloat("-ubound");
	}
	fractal_opt.lbound = lbound;
	fractal_opt.rbound = rbound;
	fractal_opt.bbound = bbound;
	fractal_opt.ubound = ubound;

	// end arguments

	std::ostringstream ss;
	ss << "tiles/" << fractal_opt.type << '/' << color_opt.method;
	std::filesystem::create_directories(ss.str());

	// if Ctrl+C is pressed, stop iteration and save partial image
	auto ctrl_c_handler = [](const int signal)
	{
		cancel = true;
	};
	signal(SIGINT, ctrl_c_handler);

	createFractal();

	return 0;
}
