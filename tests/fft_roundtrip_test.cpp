// Exercise the scalar implementation even when the library enables SSE: the
// float SSE specialization uses a different FFT implementation.
#ifdef CLUNK_USES_SSE
#undef CLUNK_USES_SSE
#endif
#include "fft_context.h"

#include <array>
#include <iostream>
#include <limits>

template<int BITS, typename T>
bool round_trip(const std::array<std::complex<T>, 1 << BITS> &input, const char *pattern) {
	clunk::fft_context<BITS, T> fft;
	for (int i = 0; i < fft.N; ++i)
		fft.data[i] = input[i];
	fft.fft();
	fft.ifft();

	const T tolerance = std::numeric_limits<T>::epsilon() * fft.N * 8;
	for (int i = 0; i < fft.N; ++i) {
		const T error = std::abs(fft.data[i] - input[i]);
		if (!(error <= tolerance)) {
			std::cerr << "FFT round trip failed: N=" << fft.N
				<< ", precision=" << std::numeric_limits<T>::digits
				<< ", pattern=" << pattern << ", index=" << i
				<< ", expected=" << input[i] << ", actual=" << fft.data[i]
				<< ", error=" << error << '\n';
			return false;
		}
	}
	return true;
}

template<int BITS, typename T>
bool check_size() {
	const int size = 1 << BITS;
	std::array<std::complex<T>, size> input = {};
	if (!round_trip<BITS, T>(input, "zero"))
		return false;

	// Check every position, including the N=8 impulse at index 2 which used
	// to move to index 6 because the inverse butterfly kept the forward sign.
	for (int position = 0; position < size; ++position) {
		input.fill(std::complex<T>());
		input[position] = std::complex<T>(1, 0);
		if (!round_trip<BITS, T>(input, "impulse"))
			return false;
	}

	input.fill(std::complex<T>(1, 0));
	if (!round_trip<BITS, T>(input, "constant"))
		return false;
	for (int i = 0; i < size; ++i)
		input[i] = std::complex<T>(T((i * 7) % 19 - 9) / 10, 0);
	if (!round_trip<BITS, T>(input, "real"))
		return false;
	for (int i = 0; i < size; ++i)
		input[i].imag(T((i * 11) % 23 - 11) / 12);
	return round_trip<BITS, T>(input, "complex");
}

template<typename T>
bool check_sizes() {
	return check_size<1, T>() && check_size<2, T>() && check_size<3, T>()
		&& check_size<4, T>() && check_size<5, T>() && check_size<6, T>()
		&& check_size<7, T>() && check_size<8, T>();
}

int main() {
	const bool float_ok = check_sizes<float>();
	const bool double_ok = check_sizes<double>();
	return float_ok && double_ok ? 0 : 1;
}
