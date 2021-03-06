﻿#include "pch.h"
#include <iostream>
#include <vector>
#include <deque>
#include <iterator>
#include <cmath>
#include <fstream>
#include <SFML/Graphics.hpp>
#include <SFML/Audio.hpp>

#include <Windows.h>
#define PI 3.141592653589793238462643383279502884197169399375105820974944592

inline double KeyToFreq(double Key) {
	return 440. * std::pow(2, (Key - 69.) / 12.);
}
inline double GetDFTCoeficient(INT16* Samples,DWORD SampleAmount, double TicksPerSample) {
	double Acc = 0, Num = -2.f*PI*TicksPerSample / SampleAmount;
	for (int t = 0; t < SampleAmount; ++t) {
		Acc += std::cos(Num*t)*(Samples[t]/32767.f)*2.f;
	}
	return Acc;
}
inline void DFT(INT16* Samples, DWORD SampleAmount, std::vector<double>&Hertz, double*& Output, double SamplesPerSecond) {//hertz = ticks/second
	for (int i = 0; i < Hertz.size(); i++) {
		Output[i] = abs(GetDFTCoeficient(Samples, SampleAmount, Hertz[i]/SamplesPerSecond))*(i/64.);
	}
}



int main(){
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

	std::ifstream in(Filename,std::ios::binary | std::ios::in);
	std::ofstream out;
	std::vector<BYTE> Data;
	std::vector<double> Hertz;
	std::deque<BYTE> FinalTrack;
	double* Output;
	INT16 *Samples;
	DWORD NotesPerSecond;
	DWORD SampleRate;
	BYTE T;
	BOOL First = 1;
	size_t filesize,SampleArraySize;
	sf::InputSoundFile ISD;

	in.seekg(0, std::ios::end);
	filesize = in.tellg();
	in.seekg(0, std::ios::beg);

	Data.reserve(filesize+5);
	while (in.good() && !in.eof()) {
		Data.push_back(in.get());
	}
	in.close();

	if (ISD.openFromMemory(Data.data(), Data.size())) {
		std::cout << "Type in amount of ticks per second in the output midi.\nIf you don't understand what this means, type in 0. Default value will be assigned\nType in... ";
		std::cin >> NotesPerSecond;
		if (NotesPerSecond<1)NotesPerSecond = 64;

		Output = new double[128];
		for (int i = 0; i < 128; i++)
			Hertz.push_back(KeyToFreq((float)i));
		SampleRate = ISD.getSampleRate()*ISD.getChannelCount();
		Samples = new INT16[(SampleArraySize = SampleRate/NotesPerSecond)];
		//TEMPO = 60000000.;
		std::cout << "Things are prepaired\n";


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
			First = 1;
			for (int i = 0; i < 128; i++) {
				T = (Output[i] < 0) ? 0 : (Output[i] > 128) ? 128 : Output[i];
				if (!T)continue;
				FinalTrack.push_back(0);
				FinalTrack.push_back(0x90 | (T >> 4));
				FinalTrack.push_back(i);
				FinalTrack.push_back(T);
			}
			for (int i = 0; i < 128; i++) {
				T = (Output[i] < 0) ? 0 : (Output[i] > 128) ? 128 : Output[i];
				if (!T)continue;
				FinalTrack.push_back(First);
				FinalTrack.push_back(0x80 | (T >> 4));// *(FinalTrack.rend() + 514)&0xF
				FinalTrack.push_back(i);
				FinalTrack.push_back(0x40);
				if (First)First = 0;
			}
		} while (SampleArraySize);
		FinalTrack.push_back(0);
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
		out.put(NotesPerSecond>>8); out.put(NotesPerSecond&0xFF);//PPQN
		out.put('M'); out.put('T'); out.put('r'); out.put('k');
		out.put(FinalTrack.size()>>24);
		out.put((FinalTrack.size() >> 16)&0xFF);
		out.put((FinalTrack.size() >> 8) & 0xFF);
		out.put(FinalTrack.size() & 0xFF);
		std::copy(FinalTrack.begin(), FinalTrack.end(), std::ostream_iterator<BYTE>(out));
		out.close();
	}

	system("pause");
	return 0;
}