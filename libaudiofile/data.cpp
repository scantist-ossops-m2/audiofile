/*
	Audio File Library
	Copyright (C) 1998-2000, Michael Pruett <michael@68k.org>
	Copyright (C) 2000, Silicon Graphics, Inc.

	This library is free software; you can redistribute it and/or
	modify it under the terms of the GNU Library General Public
	License as published by the Free Software Foundation; either
	version 2 of the License, or (at your option) any later version.

	This library is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
	Library General Public License for more details.

	You should have received a copy of the GNU Library General Public
	License along with this library; if not, write to the
	Free Software Foundation, Inc., 59 Temple Place - Suite 330,
	Boston, MA  02111-1307  USA.
*/

/*
	data.cpp
*/

#include "config.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "Track.h"
#include "af_vfs.h"
#include "afinternal.h"
#include "audiofile.h"
#include "modules/Module.h"
#include "modules/ModuleState.h"
#include "util.h"

int afWriteFrames (AFfilehandle file, int trackid, const void *samples,
	int nvframes2write)
{
	SharedPtr<Module> firstmod;
	SharedPtr<Chunk> userc;
	Track *track;
	int bytes_per_vframe;
	AFframecount vframe;

	if (!_af_filehandle_ok(file))
		return -1;

	if (!_af_filehandle_can_write(file))
		return -1;

	if ((track = _af_filehandle_get_track(file, trackid)) == NULL)
		return -1;

	if (track->ms->isDirty() && track->ms->setup(file, track) == AF_FAIL)
		return -1;

	/*if (file->seekok) {*/

	if (af_fseek(file->fh, track->fpos_next_frame, SEEK_SET) < 0)
	{
		_af_error(AF_BAD_LSEEK, "unable to position write pointer at next frame");
		return -1;
	}

	/* } */

	bytes_per_vframe = _af_format_frame_size(&track->v, true);

	firstmod = track->ms->modules().front();
	userc = track->ms->chunks().front();

	track->filemodhappy = true;

	vframe = 0;
#ifdef UNLIMITED_CHUNK_NVFRAMES
	/*
		OPTIMIZATION: see the comment at the very end of
		arrangemodules() in modules.c for an explanation of this:
	*/
	if (!trk->ms->mustUseAtomicNVFrames())
	{
		userc->buffer = (char *) samples;
		userc->frameCount = nvframes2write;

		firstmod->runPush();

		/* Count this chunk if there was no i/o error. */
		if (trk->filemodhappy)
			vframe += userc->frameCount;
	}
	else
#else
	/* Optimization must be off. */
	assert(track->ms->mustUseAtomicNVFrames());
#endif
	{
		while (vframe < nvframes2write)
		{
			userc->buffer = (char *) samples + bytes_per_vframe * vframe;
			if (vframe <= nvframes2write - _AF_ATOMIC_NVFRAMES)
				userc->frameCount = _AF_ATOMIC_NVFRAMES;
			else
				userc->frameCount = nvframes2write - vframe;

			firstmod->runPush();

			if (!track->filemodhappy)
				break;

			vframe += userc->frameCount;
		}
	}

	track->nextvframe += vframe;
	track->totalvframes += vframe;

	return vframe;
}

int afReadFrames (AFfilehandle file, int trackid, void *samples,
	int nvframeswanted)
{
	Track	*track;
	SharedPtr<Module> firstmod;
	SharedPtr<Chunk> userc;
	AFframecount	nvframesleft, nvframes2read;
	int		bytes_per_vframe;
	AFframecount	vframe;

	if (!_af_filehandle_ok(file))
		return -1;

	if (!_af_filehandle_can_read(file))
		return -1;

	if ((track = _af_filehandle_get_track(file, trackid)) == NULL)
		return -1;

	if (track->ms->isDirty() && track->ms->setup(file, track) == AF_FAIL)
		return -1;

	/*if (file->seekok) {*/

	if (af_fseek(file->fh, track->fpos_next_frame, SEEK_SET) < 0)
	{
		_af_error(AF_BAD_LSEEK, "unable to position read pointer at next frame");
		return -1;
	}

	/* } */

	if (track->totalvframes == -1)
		nvframes2read = nvframeswanted;
	else
	{
		nvframesleft = track->totalvframes - track->nextvframe;
		nvframes2read = (nvframeswanted > nvframesleft) ?
			nvframesleft : nvframeswanted;
	}
	bytes_per_vframe = _af_format_frame_size(&track->v, true);

	firstmod = track->ms->modules().back();
	userc = track->ms->chunks().back();

	track->filemodhappy = true;

	vframe = 0;

	if (!track->ms->mustUseAtomicNVFrames())
	{
		assert(track->frames2ignore == 0);
		userc->buffer = samples;
		userc->frameCount = nvframes2read;

		firstmod->runPull();
		if (track->filemodhappy)
			vframe += userc->frameCount;
	}
	else
	{
		bool	eof = false;

		if (track->frames2ignore != 0)
		{
			userc->frameCount = track->frames2ignore;
			userc->allocate(track->frames2ignore * bytes_per_vframe);
			if (userc->buffer == NULL)
				return 0;

			firstmod->runPull();

			/* Have we hit EOF? */
			if (userc->frameCount < track->frames2ignore)
				eof = true;

			track->frames2ignore = 0;

			free(userc->buffer);
			userc->buffer = NULL;
		}

		/*
			Now start reading useful frames, until EOF or
			premature EOF.
		*/

		while (track->filemodhappy && !eof && vframe < nvframes2read)
		{
			AFframecount	nvframes2pull;
			userc->buffer = (char *) samples + bytes_per_vframe * vframe;

			if (vframe <= nvframes2read - _AF_ATOMIC_NVFRAMES)
				nvframes2pull = _AF_ATOMIC_NVFRAMES;
			else
				nvframes2pull = nvframes2read - vframe;

			userc->frameCount = nvframes2pull;

			firstmod->runPull();

			if (track->filemodhappy)
			{
				vframe += userc->frameCount;
				if (userc->frameCount < nvframes2pull)
					eof = true;
			}
		}
	}

	track->nextvframe += vframe;

	return vframe;
}
