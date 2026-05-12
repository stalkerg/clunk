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
	SDL_AudioDeviceID current_audio_device_id = 0;
}

SDL_AudioDeviceID clunk::get_audio_device_id() {
	return current_audio_device_id;
}

void clunk::set_audio_device_id(SDL_AudioDeviceID device_id) {
	current_audio_device_id = device_id;
}

void clunk::lock_audio() {
	SDL_AudioDeviceID device_id = get_audio_device_id();
	if (device_id != 0)
		SDL_LockAudioDevice(device_id);
}

void clunk::unlock_audio() {
	SDL_AudioDeviceID device_id = get_audio_device_id();
	if (device_id != 0)
		SDL_UnlockAudioDevice(device_id);
}

clunk::AudioLocker::AudioLocker() : device_id(get_audio_device_id()) {
	if (device_id != 0)
		SDL_LockAudioDevice(device_id);
}

clunk::AudioLocker::AudioLocker(SDL_AudioDeviceID device_id) : device_id(device_id) {
	if (device_id != 0)
		SDL_LockAudioDevice(device_id);
}

clunk::AudioLocker::~AudioLocker() {
	if (device_id != 0)
		SDL_UnlockAudioDevice(device_id);
}
