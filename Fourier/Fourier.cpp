#include "pch.h"
#include <iostream>
#include <vector>
#include <array>
#include <iterator>
#include <cmath>
#include <fstream>
#include <complex>
#include <functional>
#include <SFML/Graphics.hpp>
#include <SFML/Audio.hpp>

#include <Windows.h>
#define PI 3.141592653589793238462643383279502884197169399375105820974944592

inline double KeyToFreq(double Key) {
	return 440. * std::pow(2, (Key - 69.) / 12.);
}
inline double GetDFTCoeficient(INT16* Samples, DWORD SampleAmount, double TicksPerSample) {
	double Num = -2.f * PI * TicksPerSample / SampleAmount;
	std::complex<double> Acc = 0;
	for (int t = 0; t < SampleAmount; ++t) {
		Acc += (std::cos(Num * t) + std::complex<double>(0, 1) * std::sin(Num * t)) * (Samples[t] / 32767.) * 2.;
	}
	return std::abs(Acc);
}
inline void DFT(INT16* Samples, DWORD SampleAmount, std::vector<double>& Hertz, double*& Output, double SamplesPerSecond) {//hertz = ticks/second
	for (int i = 0; i < Hertz.size(); i++) {
		Output[i] = GetDFTCoeficient(Samples, SampleAmount, Hertz[i] / SamplesPerSecond);
	}
}

int main() {
	system("mode CON: COLS=90 LINES=15");
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

	std::ifstream in(Filename, std::ios::binary | std::ios::in);
	std::ofstream out;
	std::vector<BYTE> Data;
	std::vector<double> Hertz;
	std::vector<BYTE> FinalTrack;

	std::vector<std::array<double, 128>> Velocities;
	double maxVelocity = -1;

	double* Output;
	INT16* Samples;
	DWORD NotesPerSecond;
	DWORD SampleRate;
	BYTE T;
	BOOL First = 1;
	size_t filesize, SampleArraySize;
	sf::InputSoundFile ISD;

	in.seekg(0, std::ios::end);
	filesize = in.tellg();
	in.seekg(0, std::ios::beg);

	Data.reserve(filesize + 5);
	while (in.good() && !in.eof()) {
		Data.push_back(in.get());
	}
	in.close();

	if (ISD.openFromMemory(Data.data(), Data.size())) {
		std::cout << "Type in amount of ticks per second in the output midi.\nIf you don't understand what this means, type in 0. Default value will be assigned\nType in... ";
		std::cin >> NotesPerSecond;
		if (NotesPerSecond < 1)
			NotesPerSecond = 64;

		std::function<double(double)> mapper = [](double x) {return x; };
		std::cout << "Disable velocity remapping (often enhances audio, but also might cause audio to drown in intense parts) (enter y/Y to disable): ";
		char agreement;
		std::cin >> agreement;
		if (agreement != 'y' && agreement != 'Y')
			mapper = [](double x) {return std::sqrt(x); };

		Output = new double[128];
		for (int i = 0; i < 128; i++)
			Hertz.push_back(KeyToFreq((float)i));
		SampleRate = ISD.getSampleRate() * ISD.getChannelCount();
		Samples = new INT16[(SampleArraySize = SampleRate / NotesPerSecond)];
		//TEMPO = 60000000.;
		std::cout << "Started processing\n";

		FinalTrack.push_back(0);
		FinalTrack.push_back(0xFF);
		FinalTrack.push_back(0x51);
		FinalTrack.push_back(3);
		FinalTrack.push_back(0x0F);
		FinalTrack.push_back(0x42);
		FinalTrack.push_back(0x40);//BPM = 60!

		do {
			SampleArraySize = ISD.read(Samples, SampleArraySize);
			DFT(Samples, SampleArraySize, Hertz, Output, NotesPerSecond);
			
			Velocities.emplace_back();
			for (int i = 0; i < 128; ++i) {
				Velocities.back()[i] = mapper(Output[i]);
				if (Velocities.back()[i] > maxVelocity)
					maxVelocity = Velocities.back()[i];
			}
		} while (SampleArraySize);

		for (const auto& singleTick : Velocities) {
			First = 1;
			for (int i = 0; i < 128; i++) {
				auto curVelocity = singleTick[i] * 127 / maxVelocity;
				T = curVelocity;
				if (!T)
					continue;
				FinalTrack.push_back(0);
				FinalTrack.push_back(0x90 | (T >> 4));
				FinalTrack.push_back(i);
				FinalTrack.push_back(T);
			}
			for (int i = 0; i < 128; i++) {
				auto curVelocity = singleTick[i] * 127 / maxVelocity;
				T = curVelocity;
				if (!T)
					continue;
				FinalTrack.push_back(First);
				FinalTrack.push_back(0x80 | (T >> 4));
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

		std::cout << "Fourier has finished his work. Writing on disk :)\n";
		DeleteFile((Filename + L"." + std::to_wstring(NotesPerSecond) + L".mid").c_str());

		out = std::ofstream((Filename + L"." + std::to_wstring(NotesPerSecond) + L".mid"), std::ios::binary | std::ios::out);
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