/* libClunk - cross-platform 3D audio API built on top SDL library
 * Copyright (C) 2007-2008 Netive Media Group
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.

 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
*/

#include "locker.h"

namespace {
	SDL_AudioStream *current_audio_stream = NULL;
}

SDL_AudioStream *clunk::get_audio_stream() {
	return current_audio_stream;
}

void clunk::set_audio_stream(SDL_AudioStream *stream) {
	current_audio_stream = stream;
}

void clunk::lock_audio() {
	SDL_AudioStream *stream = get_audio_stream();
	if (stream != NULL)
		SDL_LockAudioStream(stream);
}

void clunk::unlock_audio() {
	SDL_AudioStream *stream = get_audio_stream();
	if (stream != NULL)
		SDL_UnlockAudioStream(stream);
}

clunk::AudioLocker::AudioLocker() : stream(get_audio_stream()) {
	if (stream != NULL)
		SDL_LockAudioStream(stream);
}

clunk::AudioLocker::AudioLocker(SDL_AudioStream *stream) : stream(stream) {
	if (stream != NULL)
		SDL_LockAudioStream(stream);
}

clunk::AudioLocker::~AudioLocker() {
	if (stream != NULL)
		SDL_UnlockAudioStream(stream);
}
