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


#include <SDL3/SDL.h>
#include <SDL3/SDL_audio.h>
#include "context.h"
#include <string.h>
#include "sdl_ex.h"
#include "logger.h"
#include "source.h"
#include <assert.h>
#define _USE_MATH_DEFINES
#include <math.h>
#include <map>
#include <algorithm>
#include <vector>
#include "locker.h"
#include "stream.h"
#include "object.h"

using namespace clunk;

Context::Context() : audio_stream(NULL), initialized_audio_subsystem(false), period_size(0), listener(NULL), max_sources(8), fx_volume(1), distance_model(DistanceModel::Inverse, true, 128), fdump(NULL) {
}

void SDLCALL Context::callback(void *userdata, SDL_AudioStream *stream, int additional_amount, int) {
	Context *self = (Context *)userdata;
	assert(self != NULL);
	if (additional_amount <= 0)
		return;

	TRY {
		const int frame_size = SDL_AUDIO_FRAMESIZE(self->spec);
		int frames = (additional_amount + frame_size - 1) / frame_size;
		if (self->period_size > frames)
			frames = self->period_size;
		const int len = frames * frame_size;

		self->callback_buffer.set_size(len);
		self->process((Sint16 *)self->callback_buffer.get_ptr(), len);
		if (!SDL_PutAudioStreamData(stream, self->callback_buffer.get_ptr(), len))
			throw_sdl(("SDL_PutAudioStreamData"));
	} CATCH("callback", {})
}

template<class Sources>
bool Context::process_object(Object *o, Sources &sset, std::vector<source_t> &lsources, unsigned n) {
	typedef typename std::map<typename Sources::key_type, unsigned> stats_type;
	stats_type sources_stats;
	
	for(typename Sources::iterator j = sset.begin(); j != sset.end(); ) {
		const typename Sources::key_type &name = j->first;
		Source *s = j->second;
		if (!s->playing()) {
			//LOG_DEBUG(("purging inactive source %s", j->first.c_str()));
			delete j->second;
			sset.erase(j++);
			continue;
		}
		
		typename stats_type::iterator s_i = sources_stats.find(name);
		unsigned same_sounds_n = (s_i != sources_stats.end())? s_i->second: 0;
		if (lsources.size() < max_sources && same_sounds_n < distance_model.same_sounds_limit) {
			lsources.push_back(source_t(s, o->position + s->delta_position - listener->position, o->velocity, o->direction, listener->velocity));
			if (same_sounds_n == 0) {
				sources_stats.insert(typename stats_type::value_type(name, 1));
			} else {
				++s_i->second;
			}
			//LOG_DEBUG(("%u: source: %s", (unsigned)lsources.size(), name.c_str()));
		} else {
			s->_update_position(n);
		}
		++j;
	}

	if (sset.empty() && o->dead) 
		return false;

	return true;
}

void Context::process(Sint16 *stream, int size) {
	//TIMESPY(("total"));

	{
		//TIMESPY(("sorting objects"));
		std::sort(objects.begin(), objects.end(), Object::DistanceOrder(listener->position));
	}
	//LOG_DEBUG(("sorted %u objects", (unsigned)objects.size()));
	
	std::vector<source_t> lsources;
	int n = size / 2 / spec.channels;

	for(objects_type::iterator i = objects.begin(); i != objects.end(); ) {
		Object *o = *i;
		//bool _process_object(Object *o, Sources &sset, std::vector<source_t> &lsources, unsigned max_sources, const DistanceModel &distance_model, Object *listener, unsigned n) {
		bool ok_1 = process_object<Object::NamedSources>(o, o->named_sources, lsources, n),
			ok_2 = process_object<Object::IndexedSources>(o, o->indexed_sources, lsources, n);
		if (ok_1 || ok_2) 
			++i;
		else {
			delete o;
			i = objects.erase(i);
		}
	}

	memset(stream, 0, size);

	for(streams_type::iterator i = streams.begin(); i != streams.end();) {
		//LOG_DEBUG(("processing stream %d", i->first));
		stream_info &stream_info = i->second;
		if (stream_info.paused) {
			++i;
			continue;
		}

		bool retried_empty_loop = false;
		while ((int)stream_info.buffer.get_size() < size) {
			int available = SDL_GetAudioStreamAvailable(stream_info.converter);
			if (available < 0)
				throw_sdl(("SDL_GetAudioStreamAvailable"));
			if (available > 0) {
				const int missing = size - (int)stream_info.buffer.get_size();
				const int requested = available < missing ? available : missing;
				const size_t old_size = stream_info.buffer.get_size();
				stream_info.buffer.set_size(old_size + requested);
				int received = SDL_GetAudioStreamData(stream_info.converter,
					(Uint8 *)stream_info.buffer.get_ptr() + old_size, requested);
				if (received < 0) {
					stream_info.buffer.set_size(old_size);
					throw_sdl(("SDL_GetAudioStreamData"));
				}
				stream_info.buffer.set_size(old_size + received);
				if (received > 0)
					continue;
			}

			if (stream_info.ended)
				break;

			clunk::Buffer data;
			bool eos = !stream_info.stream->read(data, size);
			if (!data.empty() && !SDL_PutAudioStreamData(stream_info.converter, data.get_ptr(), (int)data.get_size()))
				throw_sdl(("SDL_PutAudioStreamData"));
			//LOG_DEBUG(("read %u bytes", (unsigned)data.get_size()));
			if (eos) {
				if (stream_info.loop) {
					if (data.empty() && retried_empty_loop)
						break;
					stream_info.stream->rewind();
					retried_empty_loop = data.empty();
				} else {
					if (!SDL_FlushAudioStream(stream_info.converter))
						throw_sdl(("SDL_FlushAudioStream"));
					stream_info.ended = true;
				}
			} else if (data.empty()) {
				break;
			}
		}
		int buf_size = (int)stream_info.buffer.get_size();
		//LOG_DEBUG(("buffered %d bytes", buf_size));
		if (buf_size == 0 && stream_info.ended) {
			//all data buffered. continue;
			LOG_DEBUG(("stream %d finished. dropping.", i->first));
			TRY {
				delete stream_info.stream;
			} CATCH("mixing stream", {});
			if (stream_info.converter != NULL)
				SDL_DestroyAudioStream(stream_info.converter);
			streams.erase(i++);
			continue;
		}
		if (buf_size == 0) {
			++i;
			continue;
		}
		
		if (buf_size >= size)
			buf_size = size;

		if (!SDL_MixAudio((Uint8 *)stream, (const Uint8 *)stream_info.buffer.get_ptr(), spec.format, buf_size, stream_info.gain))
			throw_sdl(("SDL_MixAudio"));
		
		if ((int)stream_info.buffer.get_size() > size) {
			memmove(stream_info.buffer.get_ptr(), ((Uint8 *)stream_info.buffer.get_ptr()) + size, stream_info.buffer.get_size() - size);
			stream_info.buffer.set_size(stream_info.buffer.get_size() - size);
		} else {
			stream_info.buffer.free();
		}
		
		++i;
	}
	
	clunk::Buffer buf;
	buf.set_size(size);
	
	//TIMESPY(("mixing sources"));
	//LOG_DEBUG(("mixing %u sources", (unsigned)lsources.size()));
	for(unsigned i = 0; i < lsources.size(); ++i ) {
		const source_t& source_info = lsources[i];
		Source * source = source_info.source;
				
		float dpitch = 1.0f;
		if (distance_model.doppler_factor > 0) {
			dpitch = distance_model.doppler_pitch(-source_info.s_pos, source_info.s_vel, source_info.l_vel);
		}

		float volume = fx_volume * distance_model.gain(source_info.s_pos.length());
		if (volume <= 0)
			continue;
		//check for 0
		volume = source->_process(buf, spec.channels, source_info.s_pos, source_info.s_dir, volume, dpitch);
		//LOG_DEBUG(("%u: mixing source with volume %g", i, volume));
		if (volume <= 0)
			continue;
		if (volume > 1)
			volume = 1;
		
		if (!SDL_MixAudio((Uint8 *)stream, (const Uint8 *)buf.get_ptr(), spec.format, size, volume))
			throw_sdl(("SDL_MixAudio"));
	}
	
	if (fdump != NULL) {
		if (fwrite(stream, size, 1, fdump) != 1) {
			fclose(fdump);
			fdump = NULL;
		}
	}
}


Object *Context::create_object() {
	AudioLocker l;
	Object *o = new Object(this);
	objects.push_back(o);
	return o;
}

Sample *Context::create_sample() {
	AudioLocker l;
	return new Sample(this);
}

void Context::save(const std::string &file) {
	AudioLocker l;
	if (fdump != NULL) {
		fclose(fdump);
		fdump = NULL;
	}
	if (file.empty())
		return;
	
	fdump = fopen(file.c_str(), "wb");
}

void Context::init(const int sample_rate, const Uint8 channels, int period_size) {
	if (get_audio_stream() != NULL)
		throw_ex(("audio device is already opened"));
	if (channels < 1 || channels > 2)
		throw_ex(("Clunk requires mono or stereo output, got %d channels", channels));
	if (sample_rate <= 0)
		throw_ex(("sample rate must be positive, got %d", sample_rate));
	if (period_size <= 0)
		throw_ex(("period size must be positive, got %d", period_size));

	if (!SDL_WasInit(SDL_INIT_AUDIO)) {
		if (!SDL_InitSubSystem(SDL_INIT_AUDIO))
			throw_sdl(("SDL_InitSubSystem"));
		initialized_audio_subsystem = true;
	}

	spec.format = SDL_AUDIO_S16;
	spec.channels = channels;
	spec.freq = sample_rate;
	this->period_size = period_size;
	
	audio_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, &Context::callback, this);
	if (audio_stream == NULL) {
		std::string error = SDL_GetError();
		if (initialized_audio_subsystem) {
			SDL_QuitSubSystem(SDL_INIT_AUDIO);
			initialized_audio_subsystem = false;
		}
		throw_ex(("SDL_OpenAudioDeviceStream(%d, %u): %s", sample_rate, channels, error.c_str()));
	}

	set_audio_stream(audio_stream);
	TRY {
		AudioLocker l;
		listener = create_object();
	} CATCH("Context::init", {
		set_audio_stream(NULL);
		SDL_DestroyAudioStream(audio_stream);
		audio_stream = NULL;
		if (initialized_audio_subsystem) {
			SDL_QuitSubSystem(SDL_INIT_AUDIO);
			initialized_audio_subsystem = false;
		}
		throw;
	})

	if (!SDL_ResumeAudioStreamDevice(audio_stream)) {
		std::string error = SDL_GetError();
		deinit();
		throw_ex(("SDL_ResumeAudioStreamDevice: %s", error.c_str()));
	}

	SDL_AudioDeviceID device_id = SDL_GetAudioStreamDevice(audio_stream);
	SDL_AudioSpec device_spec;
	int device_sample_frames = 0;
	if (SDL_GetAudioDeviceFormat(device_id, &device_spec, &device_sample_frames)) {
		LOG_DEBUG(("opened SDL3 audio stream on device %u, mixer: %d Hz/%d channels, device: %d Hz/%d channels/%d sample frames",
			(unsigned)device_id, spec.freq, spec.channels, device_spec.freq, device_spec.channels, device_sample_frames));
	} else {
		LOG_DEBUG(("opened SDL3 audio stream on device %u, mixer: %d Hz/%d channels",
			(unsigned)device_id, spec.freq, spec.channels));
	}
}

void Context::delete_object(Object *o) {
	AudioLocker l;
	objects_type::iterator i = std::find(objects.begin(), objects.end(), o);
	while(i != objects.end() && *i == o)
		i = objects.erase(i); //just for fun
}

void Context::deinit() {
	//cleanup objects here too.
	SDL_AudioStream *opened_audio_stream = audio_stream;
	if (opened_audio_stream != NULL) {
		SDL_PauseAudioStreamDevice(opened_audio_stream);
		AudioLocker l(opened_audio_stream);
		delete listener;
		listener = NULL;
		for(streams_type::iterator i = streams.begin(); i != streams.end(); ++i) {
			delete i->second.stream;
			if (i->second.converter != NULL)
				SDL_DestroyAudioStream(i->second.converter);
		}
		streams.clear();
	} else {
		delete listener;
		listener = NULL;
	}

	if (opened_audio_stream != NULL) {
		if (get_audio_stream() == opened_audio_stream)
			set_audio_stream(NULL);
		SDL_DestroyAudioStream(opened_audio_stream);
		audio_stream = NULL;
	}
	
	if (fdump != NULL) {
		fclose(fdump);
		fdump = NULL;
	}

	if (initialized_audio_subsystem) {
		SDL_QuitSubSystem(SDL_INIT_AUDIO);
		initialized_audio_subsystem = false;
	}
}
	
Context::~Context() {
	deinit();
}


//MUSIC MIXER: 

void Context::play(const int id, Stream *stream, bool loop) {
	LOG_DEBUG(("play(%d, %p, %s)", id, (const void *)stream, loop?"'loop'":"'once'"));
	if (stream == NULL)
		throw_ex(("play(%d) called with a null stream", id));

	AudioLocker l;
	SDL_AudioSpec src_spec;
	src_spec.format = stream->format;
	src_spec.channels = stream->channels;
	src_spec.freq = stream->sample_rate;
	SDL_AudioStream *converter = SDL_CreateAudioStream(&src_spec, &spec);
	if (converter == NULL)
		throw_sdl(("SDL_CreateAudioStream"));

	stream_info & stream_info = streams[id];
	delete stream_info.stream;
	if (stream_info.converter != NULL)
		SDL_DestroyAudioStream(stream_info.converter);
	stream_info.buffer.free();
	stream_info.stream = stream;
	stream_info.converter = converter;
	stream_info.loop = loop;
	stream_info.ended = false;
	stream_info.paused = false;
	stream_info.gain = 1.0f;
}

bool Context::playing(const int id) const {
	AudioLocker l;
	return streams.find(id) != streams.end();
}

void Context::pause(const int id) {
	AudioLocker l;
	streams_type::iterator i = streams.find(id);
	if (i == streams.end())
		return;
	
	i->second.paused = !i->second.paused;
}

void Context::stop(const int id) {
	AudioLocker l;
	streams_type::iterator i = streams.find(id);
	if (i == streams.end())
		return;
	
	TRY {
		delete i->second.stream;
	} CATCH(clunk::format_string("stop(%d)", id).c_str(), {
		if (i->second.converter != NULL)
			SDL_DestroyAudioStream(i->second.converter);
		streams.erase(i);
		throw;
	})
	if (i->second.converter != NULL)
		SDL_DestroyAudioStream(i->second.converter);
	streams.erase(i);
}

void Context::set_volume(const int id, float volume) {
	AudioLocker l;
	if (volume < 0)
		volume = 0;
	if (volume > 1)
		volume = 1;
		
	streams_type::iterator i = streams.find(id);
	if (i == streams.end())
		return;
	i->second.gain = volume;
}

void Context::set_fx_volume(float volume) {
	AudioLocker l;
	//LOG_WARN(("ignoring set_fx_volume(%g)", volume));
	if (volume  < 0)	
		fx_volume = 0;
	else if (volume > 1)
		fx_volume = 1;
	else 
		fx_volume = volume;
}

bool Context::set_paused(bool paused) {
	if (audio_stream == NULL)
		return true;
	return paused ? SDL_PauseAudioStreamDevice(audio_stream) : SDL_ResumeAudioStreamDevice(audio_stream);
}

void Context::stop_all() {
	AudioLocker l;
	for(streams_type::iterator i = streams.begin(); i != streams.end(); ++i) {
		delete i->second.stream;
		if (i->second.converter != NULL)
			SDL_DestroyAudioStream(i->second.converter);
	}
	streams.clear();
}

void Context::set_max_sources(int sources) {
	AudioLocker l;
	max_sources = sources;
}

void Context::convert(clunk::Buffer &dst, const clunk::Buffer &src, int rate, SDL_AudioFormat format, const Uint8 channels) {
	SDL_AudioSpec src_spec;
	src_spec.format = format;
	src_spec.channels = channels;
	src_spec.freq = rate;

	SDL_AudioSpec dst_spec = spec;
	dst_spec.channels = channels;

	Uint8 *converted_data = NULL;
	int converted_size = 0;
	if (!SDL_ConvertAudioSamples(&src_spec, (const Uint8 *)src.get_ptr(), (int)src.get_size(),
		&dst_spec, &converted_data, &converted_size))
		throw_sdl(("SDL_ConvertAudioSamples(%d, %04x, %u)", rate, (unsigned)format, channels));

	TRY {
		dst.set_data(converted_data, converted_size);
	} CATCH("Context::convert", {
		SDL_free(converted_data);
		throw;
	})
	SDL_free(converted_data);
}

/*!
	\mainpage Tutorial 
	\section overview Overview
	Hello there! 
	Here's quick explanation of the clunk library concepts and usage scenarios. 
	\section scenario Typical scenario

	Context initializes the SDL audio subsystem when necessary. If the application initialized it first, Context leaves it running during deinitialization.

	Let's initialize context with typical values: 22kHz sample rate, 2 channels and a minimum mixer block of 1024 sample frames:
	\code
	Context context; 
	context.init(22050, 2, 1024);
	//main code
	context.deinit();
	\endcode
	If you choose greater sample rate such as 44kHz or even 48kHz, you will need more CPU power to mix sounds and it could hurt overall game performance. 
	The period controls how much audio Clunk generates at a time. SDL3 selects and manages the actual device buffer size.
	
	Then application should load some samples to the library. Clunk itself does not provide code to decode audio formats, or load raw wave files. 
	Check ogg/vorbis library for a free production-quality audio codec. Samples allocates within context internally with clunk::Context::create_sample() method. 

	\code
	clunk::Buffer data; //placeholder for a memory chunk
	//decode ogg sample into data
	clunk::Sample *sample = Context->create_sample();
	sample->init(data, ogg_rate, SDL_AUDIO_S16LE, ogg_channels);
	\endcode
	
	So all audio data were loaded and initialized. Next step is to allocate objects. Clunk was designed to be easily integrated into programs. 
	The most useful object is clunk::Object. It could hold several playing \link clunk::Source sources \endlink. 
	You could use two different approaches here: 
		\li create global mixer proxy object and leave all clunk stuff to it, such as mapping your objects to clunk ones. 
		\li directly include clunk::Object pointer into every object in game or program. 

	You wont ever need to track objects and/or manage its destruction, clunk will do it itself. 
	Example allowing sound to play after your object's death: 
	\code
	clunk::Object *clunk_object; 
	
	GameObject::~GameObject() {
		if (clunk_object != NULL) {
			clunk_object->autodelete(); //destroy me! 
			clunk_object = NULL; //leave destruction to the clunk::Context
		}
	}
	\endcode
	
	So the next step is source management. It's the most easiest part. Each source connects to its audio sample. 
	Source holds data about actual playing sound: position in wave data, pitch, gain and distance. It processes audio data 
	and simulate 3d sound positioning with hrtf function. 
	
	Creating source and adding it to the object : (the most easiest part)
	\code
		clunk_object->play("voice", new Source(yeti_sound_sample)); // no loop, no pitch, no gain adjustments. 
	\endcode
	Sources are automatically purged from the object when they are not needed anymore. So, you don't need to worry about its deletion or any management. 
	Anyway, you could cancel any playing source: 
	\code 
		clunk_object->cancel("voice"); 
	\endcode
	
	Or cancel all sounds from this object at once: 
	\code 
		clunk_object->cancel_all(true);
	\endcode
	
	\section positioning Object positioning
	Usually objects are positioning the some sort of ticking function called every frame or from the on_object_update callback. 
	Positioning is really simple: 
	\code
		clunk_object->update(clunk::v3<float>(x, y, z), clunk::v3<float>(velocity_x, velocity_y, velocity_z), clunk::v3<float>(direction_x, direction_y, direction_z));
	\endcode
	Moving listener is easy too, listener is regular clunk::Object, but it's stored in clunk::Context and holds information about your position
	\code
		context.get_listener()->update(clunk::v3<float>(x, y, z), clunk::v3<float>(velocity_x, velocity_y, velocity_z), clunk::v3<float>(direction_x, direction_y, direction_z));
	\endcode
	
	\section streaming Playing music and ambient sounds
	Clunk is able to mix as many music streams as you want (or your CPU could handle :) ). 
	First of all you need to implement your stream class derived from the clunk::Stream. 
	Don't worry, you need to implement just 2(!) clunk-related methods to make the music play. 
	\code 
		class FooStream : public clunk::Stream {
		public: 
			void FooStream(const std::string &file) {
				//open music file. 
				//store music parameters into members : 
				sample_rate = music_rate;
				channels = music_channels;
				format = SDL_AUDIO_S16LE;
				//this values here are only for educational purpose. Don't forget to fill it with actual values from the music file!
			}
			
			void rewind() {
				//rewind your stream here
			}
			
			bool read(clunk::Buffer &data, unsigned hint) {
				//read as many data as you want, but it'd better to read around 'hint' bytes to avoid memory queue overhead. 
			}

			virtual ~FooStream() {
				//don't forget to close your stream here. Leaks are unwanted guests here. 
			}
		};
	\endcode
	
	So, the most complicated part passed by. Let the party begin !
	\code 
		context.play(0, new FooStream("data/background_music.ogg"), false); //do not loop music, look below for details.
		context.play(1, new FooStream("data/ambience_city.ogg"), true); //loops ambient
	\endcode

	There's no magic numbers here. I've chosen 0 and 1 just for fun. You could use any integer id. 42 for example. 
	Why don't I use loop == true for music ? We need it to change various tunes. Let's periodically test if music ends and restart with new tune: 
	\code 
		if (!context.playing(0)) {
			context.play(0, new FooStream(next_song));
		}
	\endcode
	
	\section final Final words from author
	I've covered almost all major topics of the clunk here in this tutorial. If you have suggestion - feel free to contact me directly. 
	Hope all this code will be useful for someone. Good luck! We're waiting for your feedback!
	
*/
