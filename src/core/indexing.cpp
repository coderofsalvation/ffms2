//  Copyright (c) 2007-2009 Fredrik Mellbin
//
//  Permission is hereby granted, free of charge, to any person obtaining a copy
//  of this software and associated documentation files (the "Software"), to deal
//  in the Software without restriction, including without limitation the rights
//  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
//  copies of the Software, and to permit persons to whom the Software is
//  furnished to do so, subject to the following conditions:
//
//  The above copyright notice and this permission notice shall be included in
//  all copies or substantial portions of the Software.
//
//  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
//  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
//  THE SOFTWARE.

#include "indexing.h"
#include <iostream>
#include <fstream>
#include <algorithm>

SharedAudioContext::SharedAudioContext() {
	W64W = NULL;
	CTX = NULL;
	CurrentSample = 0;
}

SharedAudioContext::~SharedAudioContext() {
	delete W64W;
}

FFAudioContext::~FFAudioContext() {
	if (CTX)
		avcodec_close(CTX);
}

MatroskaAudioContext::MatroskaAudioContext() {
	CS = NULL;
	CodecPrivate = NULL;
}

MatroskaAudioContext::~MatroskaAudioContext() {
	if (CTX) {
		avcodec_close(CTX);
		av_free(CTX);
	}

	if (CS)
		cs_Destroy(CS);
	delete[] CodecPrivate;
}


TFrameInfo::TFrameInfo(int64_t DTS, bool KeyFrame) {
	this->DTS = DTS;
	this->KeyFrame = KeyFrame;
	this->SampleStart = 0;
	this->FilePos = 0;
	this->FrameSize = 0;
}

TFrameInfo::TFrameInfo(int64_t DTS, int64_t FilePos, unsigned int FrameSize, bool KeyFrame) {
	this->DTS = DTS;
	this->KeyFrame = KeyFrame;
	this->SampleStart = 0;
	this->FilePos = FilePos;
	this->FrameSize = FrameSize;
}

TFrameInfo::TFrameInfo(int64_t DTS, int64_t SampleStart, bool KeyFrame) {
	this->DTS = DTS;
	this->KeyFrame = KeyFrame;
	this->SampleStart = SampleStart;
	this->FilePos = 0;
	this->FrameSize = 0;
}

TFrameInfo::TFrameInfo(int64_t DTS, int64_t SampleStart, int64_t FilePos, unsigned int FrameSize, bool KeyFrame) {
	this->DTS = DTS;
	this->KeyFrame = KeyFrame;
	this->SampleStart = SampleStart;
	this->FilePos = FilePos;
	this->FrameSize = FrameSize;
}

int FFTrack::WriteTimecodes(const char *TimecodeFile, char *ErrorMsg, unsigned MsgSize) {
	std::ofstream Timecodes(TimecodeFile, std::ios::out | std::ios::trunc);

	if (!Timecodes.is_open()) {
		snprintf(ErrorMsg, MsgSize, "Failed to open '%s' for writing", TimecodeFile);
		return 1;
	}

	Timecodes << "# timecode format v2\n";

	for (iterator Cur=begin(); Cur!=end(); Cur++)
		Timecodes << std::fixed << ((Cur->DTS * TB.Num) / (double)TB.Den) << "\n";

	return 0;
}

int FFTrack::FrameFromDTS(int64_t DTS) {
	for (int i = 0; i < static_cast<int>(size()); i++)
		if (at(i).DTS == DTS)
			return i;
	return -1;
}

int FFTrack::ClosestFrameFromDTS(int64_t DTS) {
	int Frame = 0;
	int64_t BestDiff = 0xFFFFFFFFFFFFFFLL; // big number
	for (int i = 0; i < static_cast<int>(size()); i++) {
		int64_t CurrentDiff = FFABS(at(i).DTS - DTS);
		if (CurrentDiff < BestDiff) {
			BestDiff = CurrentDiff;
			Frame = i;
		}
	}

	return Frame;
}

int FFTrack::FindClosestVideoKeyFrame(int Frame) {
	Frame = FFMIN(FFMAX(Frame, 0), static_cast<int>(size()) - 1);
	for (int i = Frame; i > 0; i--)
		if (at(i).KeyFrame)
			return i;
	return 0;
}

int FFTrack::FindClosestAudioKeyFrame(int64_t Sample) {
	for (size_t i = 0; i < size(); i++) {
		if (at(i).SampleStart == Sample && at(i).KeyFrame)
			return i;
		else if (at(i).SampleStart > Sample && at(i).KeyFrame)
			return i  - 1;
	}
	return size() - 1;
}

FFTrack::FFTrack() {
	this->TT = FFMS_TYPE_UNKNOWN;
	this->TB.Num = 0;
	this->TB.Den = 0;
}

FFTrack::FFTrack(int64_t Num, int64_t Den, FFMS_TrackType TT) {
	this->TT = TT;
	this->TB.Num = Num;
	this->TB.Den = Den;
}

static bool DTSComparison(TFrameInfo FI1, TFrameInfo FI2) {
	return FI1.DTS < FI2.DTS;
}

void FFIndex::Sort() {
	for (FFIndex::iterator Cur=begin(); Cur!=end(); Cur++)
		std::sort(Cur->begin(), Cur->end(), DTSComparison);
}

int FFIndex::WriteIndex(const char *IndexFile, char *ErrorMsg, unsigned MsgSize) {
	std::ofstream IndexStream(IndexFile, std::ios::out | std::ios::binary | std::ios::trunc);

	if (!IndexStream.is_open()) {
		snprintf(ErrorMsg, MsgSize, "Failed to open '%s' for writing", IndexFile);
		return 1;
	}

	// Write the index file header
	IndexHeader IH;
	IH.Id = INDEXID;
	IH.Version = INDEXVERSION;
	IH.Tracks = size();
	IH.Decoder = Decoder;
	IH.LAVUVersion = avutil_version();
	IH.LAVFVersion = avformat_version();
	IH.LAVCVersion = avcodec_version();
	IH.LSWSVersion = swscale_version();
	IH.LPPVersion = postproc_version();

	IndexStream.write(reinterpret_cast<char *>(&IH), sizeof(IH));

	for (unsigned int i = 0; i < IH.Tracks; i++) {
		FFMS_TrackType TT = at(i).TT;
		IndexStream.write(reinterpret_cast<char *>(&TT), sizeof(TT));
		int64_t Num = at(i).TB.Num;
		IndexStream.write(reinterpret_cast<char *>(&Num), sizeof(Num));
		int64_t Den = at(i).TB.Den;
		IndexStream.write(reinterpret_cast<char *>(&Den), sizeof(Den));
		int64_t Frames = at(i).size();
		IndexStream.write(reinterpret_cast<char *>(&Frames), sizeof(Frames));

		for (FFTrack::iterator Cur=at(i).begin(); Cur!=at(i).end(); Cur++)
			IndexStream.write(reinterpret_cast<char *>(&*Cur), sizeof(TFrameInfo));
	}

	return 0;
}

int FFIndex::ReadIndex(const char *IndexFile, char *ErrorMsg, unsigned MsgSize) {
	std::ifstream Index(IndexFile, std::ios::in | std::ios::binary);

	if (!Index.is_open()) {
		snprintf(ErrorMsg, MsgSize, "Failed to open '%s' for reading", IndexFile);
		return 1;
	}

	// Read the index file header
	IndexHeader IH;
	Index.read(reinterpret_cast<char *>(&IH), sizeof(IH));
	if (IH.Id != INDEXID) {
		snprintf(ErrorMsg, MsgSize, "'%s' is not a valid index file", IndexFile);
		return 2;
	}

	if (IH.Version != INDEXVERSION) {
		snprintf(ErrorMsg, MsgSize, "'%s' is not the expected index version", IndexFile);
		return 3;
	}

	if (IH.LAVUVersion != avutil_version() || IH.LAVFVersion != avformat_version() ||
		IH.LAVCVersion != avcodec_version() || IH.LSWSVersion != swscale_version() ||
		IH.LPPVersion != postproc_version()) {
		snprintf(ErrorMsg, MsgSize, "A different FFmpeg build was used to create '%s'", IndexFile);
		return 4;
	}

	try {
		Decoder = IH.Decoder;

		for (unsigned int i = 0; i < IH.Tracks; i++) {
			// Read how many records belong to the current stream
			FFMS_TrackType TT;
			Index.read(reinterpret_cast<char *>(&TT), sizeof(TT));
			int64_t Num;
			Index.read(reinterpret_cast<char *>(&Num), sizeof(Num));
			int64_t Den;
			Index.read(reinterpret_cast<char *>(&Den), sizeof(Den));
			int64_t Frames;
			Index.read(reinterpret_cast<char *>(&Frames), sizeof(Frames));
			push_back(FFTrack(Num, Den, TT));

			TFrameInfo FI(0, false);
			for (size_t j = 0; j < Frames; j++) {
				Index.read(reinterpret_cast<char *>(&FI), sizeof(TFrameInfo));
				at(i).push_back(FI);
			}
		}

	} catch (...) {
		snprintf(ErrorMsg, MsgSize, "Unknown error while reading index information in '%s'", IndexFile);
		return 5;
	}

	return 0;
}

void FFIndexer::SetIndexMask(int IndexMask) {
	this->IndexMask = IndexMask;
}

void FFIndexer::SetDumpMask(int DumpMask) {
	this->DumpMask = DumpMask;
}

void FFIndexer::SetIgnoreDecodeErrors(bool IgnoreDecodeErrors) {
	this->IgnoreDecodeErrors = IgnoreDecodeErrors;
}

void FFIndexer::SetProgressCallback(TIndexCallback IC, void *ICPrivate) {
	this->IC = IC;
	this->ICPrivate = ICPrivate;
}

void FFIndexer::SetAudioNameCallback(TAudioNameCallback ANC, void *ANCPrivate) {
	this->ANC = ANC;
	this->ANCPrivate = ANCPrivate;
}

FFIndexer *FFIndexer::CreateFFIndexer(const char *Filename, char *ErrorMsg, unsigned MsgSize) {
	AVFormatContext *FormatContext = NULL;

	if (av_open_input_file(&FormatContext, Filename, NULL, 0, NULL) != 0) {
		snprintf(ErrorMsg, MsgSize, "Can't open '%s'", Filename);
		return NULL;
	}

	// Do matroska indexing instead?
	if (!strcmp(FormatContext->iformat->name, "matroska")) {
		av_close_input_file(FormatContext);
		return new FFMatroskaIndexer(Filename, ErrorMsg, MsgSize);
	}

#ifdef HAALISOURCE
	// Do haali ts indexing instead?
	if (!strcmp(FormatContext->iformat->name, "mpeg") || !strcmp(FormatContext->iformat->name, "mpegts")) {
		av_close_input_file(FormatContext);
		return new FFHaaliIndexer(Filename, 0, ErrorMsg, MsgSize);
	}

	if (!strcmp(FormatContext->iformat->name, "ogg")) {
		av_close_input_file(FormatContext);
		return new FFHaaliIndexer(Filename, 1, ErrorMsg, MsgSize);
	}
#endif

	return new FFLAVFIndexer(Filename, FormatContext, ErrorMsg, MsgSize);
}

FFIndexer::FFIndexer() {
	DecodingBuffer = new int16_t[AVCODEC_MAX_AUDIO_FRAME_SIZE * 5];
}

FFIndexer::~FFIndexer() {
	delete[] DecodingBuffer;
}

bool FFIndexer::WriteAudio(SharedAudioContext &AudioContext, FFIndex *Index, int Track, int DBSize, char *ErrorMsg, unsigned MsgSize) {
	// Delay writer creation until after an audio frame has been decoded. This ensures that all parameters are known when writing the headers.
	if (DBSize > 0) {
		if (!AudioContext.W64W) {
			TAudioProperties AP;
			FillAP(AP, AudioContext.CTX, (*Index)[Track]);
			int FNSize = (*ANC)(SourceFile, Track, &AP, NULL, 0, ANCPrivate);
			char *WName = new char[FNSize];
			(*ANC)(SourceFile, Track, &AP, WName, FNSize, ANCPrivate);
			std::string WN(WName);
			delete[] WName;
			try {
				AudioContext.W64W = new Wave64Writer(WN.c_str(), av_get_bits_per_sample_format(AudioContext.CTX->sample_fmt),
					AudioContext.CTX->channels, AudioContext.CTX->sample_rate, (AudioContext.CTX->sample_fmt == SAMPLE_FMT_FLT) || (AudioContext.CTX->sample_fmt == SAMPLE_FMT_DBL));
			} catch (...) {
				snprintf(ErrorMsg, MsgSize, "Failed to write wave data");
				return false;
			}
		}

		AudioContext.W64W->WriteData(DecodingBuffer, DBSize);
	}

	return true;
}