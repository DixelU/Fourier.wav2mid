#include "pch.h"
#include <iostream>
#include <vector>
#include <array>
#include <iterator>
#include <cmath>
#include <fstream>
#include <complex>
#include <functional>
#include <SFML/Audio.hpp> 

#include <Windows.h>

#include "bbb_ffio.h"
#define PI 3.141592653589793238462643383279502884197169399375105820974944592


constexpr int keywidth = 128;
constexpr int lastkey = keywidth - 1;

inline char velocityConversion(char value) {
	value /= 15;
	return value + (value >= 10);
}

inline double KeyToFreq(double Key) {
	return 440. * std::pow(2, (Key - 69.) / 12.);
}
inline double GetDFTCoeficient(int16_t* Samples, size_t SampleAmount, double TicksPerSample) {
	double Num = -2.f * PI * TicksPerSample / SampleAmount;
	std::complex<double> Acc = 0;
	for (int t = 0; t < SampleAmount; ++t) {
		Acc += (std::cos(Num * t) + std::complex<double>(0, 1) * std::sin(Num * t)) * (Samples[t] / 32767.) * 2.;
	}
	return std::abs(Acc);
}
inline void DFT(int16_t* Samples, size_t SampleAmount, std::vector<double>& Hertz, double*& Output, double SamplesPerSecond) {//hertz = ticks/second
	for (int i = 0; i < Hertz.size(); i++) {
		Output[i] = GetDFTCoeficient(Samples, SampleAmount, Hertz[i] / SamplesPerSecond);
	}
}

template<typename T>
struct RollingBuffer
{
	std::vector<T> buffer;
	std::vector<T> appendable;

	RollingBuffer(size_t bufferSize, size_t appendableBufferSize) :
		buffer(bufferSize, 0),
		appendable(appendableBufferSize, 0)
	{
	}

	void roll()
	{
		auto targetIt = std::shift_left(buffer.begin(), buffer.end(), appendable.size());
		std::copy(appendable.begin(), appendable.end(), targetIt);
	}

	void zero_appendable(size_t fromIndex)
	{
		for (auto begin = appendable.begin() + fromIndex; begin < appendable.end(); ++begin)
			*begin = 0;
	}
};

int main() {
	//system("mode CON: COLS=90 LINES=15");
	std::cout << "OFD will open soon...\n";
	OPENFILENAME ofn;       // common dialog box structure
	wchar_t szFile[1000];       // buffer for file name

	// Initialize OPENFILENAME
	ZeroMemory(&ofn, sizeof(ofn));
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = NULL;
	ofn.lpstrFile = szFile;
	// Set lpstrFile[0] to '\0' so that GetOpenFileName does not 
	// use the contents of szFile to initialize itself.
	ofn.lpstrFile[0] = '\0';
	ofn.nMaxFile = sizeof(szFile);
	ofn.lpstrFilter = L"16 Bit WAV, OGG and FLAC\0*.wav;*.ogg;*.flac\0";
	ofn.nFilterIndex = 1;
	ofn.lpstrFileTitle = NULL;
	ofn.nMaxFileTitle = NULL;
	ofn.lpstrInitialDir = NULL;
	ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;
	while (!GetOpenFileName(&ofn));
	std::wstring Filename = L"";
	for (int i = 0; i < 1000 && szFile[i] != '\0'; i++) {
		Filename.push_back(szFile[i]);
	}

	//std::ifstream in(Filename, std::ios::binary | std::ios::in);
	bbb_ffr in(Filename.c_str());
	std::ofstream out;
	std::vector<uint8_t> Data;
	std::vector<double> Hertz;
	std::vector<uint8_t> FinalTrack;

	std::vector<std::array<double, keywidth>> Velocities;
	double maxVelocity = -1;

	double* Output;
	int16_t* Samples;
	size_t NotesPerSecond;
	size_t SampleRate;
	uint8_t T;
	bool First = 1;
	size_t filesize, SampleArraySize;
	sf::InputSoundFile ISD;

	filesize = in.tellg();

	Data.reserve(filesize + 5);
	while (in.good() && !in.eof())
		Data.push_back(in.get());
	in.close();

	if (ISD.openFromMemory(Data.data(), Data.size())) {
		std::cout << "Type in amount of ticks per second in the output midi.\nIf you don't understand what this means, type in 0. Default value will be assigned\nType in... ";
		std::cin >> NotesPerSecond;
		if (NotesPerSecond < 1)
			NotesPerSecond = 64;
		size_t DecimationCount;
		std::cout << "Type in the transform's decimations count.\nIf you don't understand what this means, type in 0. Default value will be assigned\nType in... ";
		std::cin >> DecimationCount;
		if (DecimationCount < 1)
			DecimationCount = 1;

		const int result = MessageBox(NULL, L"Enable velocity remapping? It often enhances audio, but also might cause audio to drown out at intense parts.", L"Enable velocity remapping", MB_YESNOCANCEL);
		std::wstring FileSuffixes;

		std::function<double(double)> mapper = [](double x) {return x; };
		if(result == IDYES){
			mapper = [](double x) {return std::sqrt(x); };
		}
		else
			FileSuffixes += L"r"; // raw velocity

		Output = new double[keywidth];
		for (int i = 0; i < keywidth; i++)
			Hertz.push_back(KeyToFreq((float)i));
		SampleRate = ISD.getSampleRate() * ISD.getChannelCount();
		size_t SamplesArraySize = (SampleArraySize = SampleRate / NotesPerSecond);
		auto SampleRollingBufferSize = SamplesArraySize * DecimationCount;
		float frequencyShift =  float(SampleRollingBufferSize) / SamplesArraySize;

		for (auto& hz : Hertz)
			hz *= frequencyShift;

		RollingBuffer<int16_t> rollingBuffer(SampleRollingBufferSize, SamplesArraySize);
		std::cout << "Started processing\n";

		FinalTrack.push_back(0);
		FinalTrack.push_back(0xFF);
		FinalTrack.push_back(0x51);
		FinalTrack.push_back(3);
		FinalTrack.push_back(0x0F);
		FinalTrack.push_back(0x42);
		FinalTrack.push_back(0x40);//BPM = 60!

		int check = 0;
		do {
			SampleArraySize = ISD.read(rollingBuffer.appendable.data(), rollingBuffer.appendable.size());

			rollingBuffer.zero_appendable(SampleArraySize);
			rollingBuffer.roll();

			DFT(rollingBuffer.buffer.data(), rollingBuffer.buffer.size(), Hertz, Output, NotesPerSecond);
			
			Velocities.emplace_back();
			for (int i = 0; i < keywidth; ++i) {
				Velocities.back()[i] = Output[i];
				if (Velocities.back()[i] > maxVelocity)
					maxVelocity = Velocities.back()[i];
			}
			if (check / SampleRate > 60) {
				check = 0;
				std::cout << "Total seconds analysed: " << ISD.getTimeOffset().asSeconds() << std::endl;
			}
			++check;
		} while (SampleArraySize);

		std::cout << "Finished analysis... Exporting..." << std::endl;

		for (const auto& singleTick : Velocities) {
			First = 1;
			for (int i = 0; i < keywidth; ++i) {
				auto curVelocity = mapper(singleTick[i] / maxVelocity) * 127;
				T = curVelocity;
				if (!T)
					continue;
				FinalTrack.push_back(0);
				FinalTrack.push_back(0x90 | velocityConversion(T));
				FinalTrack.push_back(i);
				FinalTrack.push_back(T);
			}
			for (int i = 0; i < keywidth; ++i) {
				auto curVelocity = mapper(singleTick[i] / maxVelocity) * 127;
				T = curVelocity;
				if (!T)
					continue;
				FinalTrack.push_back(First);
				FinalTrack.push_back(0x80 | velocityConversion(T));
				FinalTrack.push_back(i);
				FinalTrack.push_back(0x40);
				if (First)
					First = 0;
			}
		}
		FinalTrack.push_back(1);
		FinalTrack.push_back(0xFF);
		FinalTrack.push_back(0x2F);
		FinalTrack.push_back(0);

		std::cout << "Writing on disk :)\n";
		DeleteFile((Filename + L"." + std::to_wstring(NotesPerSecond) + FileSuffixes + L".mid").c_str());

		out = std::ofstream((Filename + L"." + std::to_wstring(NotesPerSecond) + FileSuffixes + L".mid"), std::ios::binary | std::ios::out);
		out.put('M'); out.put('T'); out.put('h'); out.put('d');
		out.put(0); out.put(0); out.put(0); out.put(6);
		out.put(0); out.put(1);
		out.put(0); out.put(1);
		out.put(NotesPerSecond >> 8); out.put(NotesPerSecond & 0xFF);//PPQN
		out.put('M'); out.put('T'); out.put('r'); out.put('k');
		out.put(FinalTrack.size() >> 24);
		out.put((FinalTrack.size() >> 16) & 0xFF);
		out.put((FinalTrack.size() >> 8) & 0xFF);
		out.put(FinalTrack.size() & 0xFF);
		out.write((const char*)FinalTrack.data(), FinalTrack.size());
		out.close();
	}

	system("pause");
	return 0;
}