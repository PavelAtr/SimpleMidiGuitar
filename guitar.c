#include "guitar.h"
#include "midi.h"
#include "main.h"
#include "notes.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

struna struna1;


pitch_t search_pitch(note* inp, period_t period)
{
	pitch_t bend;
	if (notes[inp->index] >= period)
	{
		bend = (notes[inp->index] - period) * 100 /
			(notes[inp->index] - notes[inp->index + 1]);
//		printf("pitch upper %d %d\n", notes[inp->index] - period, notes[inp->index] - notes[inp->index + 1]);
	}
	else
	{
		bend = -((period - notes[inp->index]) * 100 / (notes[inp->index - 1] - notes[inp->index]));

//		printf("pitch lower %d %d \n", notes[inp->index] - period, notes[inp->index -1] - notes[inp->index]);
	}
	return bend;
}

/*note* search_pitch(note* inp)
{
	period_t a = notes[inp->index] - inp->period;
	period_t b = inp->period - notes[inp->index + 1];
	period_t c = notes[inp->index] - notes[inp->index + 1];

	if (a < b)
	{
		inp->bend = a * 100 / c;
	} else
	{
		inp->bend = - (b * 100 / c);
		inp->index = inp->index + 1;
	}
	
	return inp;
}

pitch_t calc_related_pitch(note* start, note* end)
{
	pitch_t ret = 0;
	ret = (start->index <= end->index) ?
		(end->index - start->index) * 100 + end->bend :
		(start->index - end->index) * 100 - end->bend;
	return ret;
}
* */

note* search_note(note* inp)
{
	for (int i = 0; i < NUMNOTES - 1; i++)
	{
		if (inp->period <= notes[i] && inp->period > notes[i + 1])
		{
			if (notes[i] - inp->period <= inp->period - notes[i + 1])
			{
				inp->search = "prev";
				inp->index = i;
			}
			else
			{
				inp->search = "next";
				inp->index = i + 1;
			}
				
			inp->bend = search_pitch(inp, inp->period);
			return inp;
		}
	}
	inp->index = -1;
	return inp;
}

note* note_copy(note* copyto, note* copyfrom)
{
	memcpy(copyto, copyfrom, sizeof(note));
	
	return copyto;
}

pitch* normalize_pitch(pitch* input, pitch_t bend)
{
	pitch_t inpitch = (bend > PITCH_MAX)	? PITCH_MAX : bend;
	inpitch = (bend < PITCH_MIN)	? PITCH_MIN : bend;
	input->bendMSB = 0;
	input->bendLSB = 0;
	pitch_t realpitch = (float)inpitch / PITCH_MAX * MIDI_PITCH_HALF + MIDI_PITCH_ZERO;
	input->realpitch = realpitch;
	input->bendMSB = (realpitch >> 7) & MIDI_PITCH_MLSB_MASK;
	input->bendLSB = realpitch & MIDI_PITCH_MLSB_MASK;
	return input;
}

byte_t normalize_velocity(int volume)
{
	volume_t velocity = volume * MIDI_VELOCITY_MAX / VOLUME_MAX ;
	velocity = (velocity >= MIDI_VELOCITY_MAX) ? MIDI_VELOCITY_MAX : velocity;
	
	return velocity;
}

void perform_freqvol(sensor_value* sensvalue, struna* str)
{
	if (sensvalue->serialno == str->serialno)
		return;
	else str->serialno = sensvalue->serialno;
	
	if (sensvalue->errors) return;

	str->oldvolume = str->curvolume;
	str->curvolume = sensvalue->volume;
	
	if (sensvalue->volume < VOLUME_NOISE)
	{
		if (!(str->flags & NOTE_SILENCE))
		{
		// End note by volume if not silence
			#ifdef DEBUGALG
			printf("End note by volume\n");
			#endif
			note_copy(&str->oldnote, &str->curnote);
			str->flags |= NOTE_END;
		}
		// Nothing else
		return;
	}
	
	flag_short_t flags = 0;

	// Searching note in array frequencys
	str->newnote.period = sensvalue->period;
	search_note(&str->newnote);
	
	if (str->newnote.index == -1) return;
	
	if (str->curvolume > str->oldvolume + VOLUME_NEW_TRESHOLD)
	{
	// New note is louder
		flags |= NOTE_NEW;
		#ifdef DEBUGALG
		printf("New note %d as louder, volume=%d period=%d\n",
			str->newnote.index, str->curvolume, str->newnote.period);
		#endif
		str->oldvolume = str->curvolume;
		goto end;
	}
	// Frequency after silence
	if (str->flags & NOTE_SILENCE)
	{
		flags |=  NOTE_NEW;
		#ifdef DEBUGALG
		printf("New note %d after silence, volume=%d, period=%d\n",
			str->newnote.index, sensvalue->volume, str->newnote.period);
		#endif
		goto end;
	}
	if (str->newnote.index == str->curnote.index)
	{
	// Note same
		if (abs(str->curnote.bend - str->newnote.bend) >= PITCH_STEP)
		{
		// if diff >= PITCH_STEP, newpitch
			str->curnote.bend = str->newnote.bend;
			flags |= NOTE_NEWPITCH;
			#ifdef DEBUGALG
			#ifdef ENABLE_BENDS
			printf("New pitch %d in same note\n", str->newnote.bend);
			#endif
			#endif
			goto end;
		}
	} else
	{
		if (abs(str->curnote.bend) < PITCH_TRESHOLD)
		{
		// Note not pitched, slide
			flags |= NOTE_NEW;
			#ifdef DEBUGALG
			printf("New note %d as new frequency, volume=%d, period=%d\n",
				str->newnote.index, sensvalue->volume, str->newnote.period);
			#endif
			goto end;
		} else
		{
			//Note pitched
			pitch_t newpitch = search_pitch(&str->curnote, str->newnote.period);
			if (abs(newpitch) > PITCH_FURTHER)
			{
				flags |= NOTE_NEW;
				#ifdef DEBUGALG
				printf("New note %d as further pitch, volume=%d\n",
					str->newnote.index + STARTMIDINOTE, sensvalue->volume);
				#endif
				goto end;
			}
			else if (abs(str->curnote.bend - newpitch) >= PITCH_STEP)
			{
				// if diff >= PITCH_STEP, newpitch
				str->curnote.bend = newpitch;
				flags |= NOTE_NEWPITCH;
				#ifdef DEBUGALG
				#ifdef ENABLE_BENDS
				printf("New further pitch %d\n", newpitch);
				#endif
				#endif
				goto end;
			}
		}
	}
end:
	if (flags & NOTE_NEW)
	{
		note_copy(&str->oldnote, &str->curnote);
		note_copy(&str->curnote, &str->newnote);
		str->curnote.volume = sensvalue->volume;
		str->curnote.accuracy = sensvalue->accuracy;
		str->flags |= NOTE_NEW | NOTE_NEWPITCH;
		if (!(str->flags & NOTE_SILENCE))
			str->flags |= NOTE_END;
	}
	if (flags & NOTE_NEWPITCH)
		str->flags |= NOTE_NEWPITCH;
}

char tmp[30];

void perform_send(struna* str)
{
	pitch tmppitch;
	
	if (str->flags & NOTE_END)
	{
		#ifdef REALMIDI				
		midiNoteOffOut(str->oldnote.index + STARTMIDINOTE,
			normalize_velocity(str->oldnote.volume), str->channel);
		#endif
		
		#ifdef DEBUGMIDI
		printf("chn=%d note END=%d velocity=%d period=%d\r\n",
			str->channel, str->oldnote.index + STARTMIDINOTE,
			normalize_velocity(str->oldnote.volume),
			str->oldnote.period);
		#endif
		
		str->flags &= ~NOTE_END;
		str->flags |= NOTE_SILENCE;
	}
	if (str->flags & NOTE_NEW)
	{
		#ifdef REALMIDI
		midiNoteOnOut(str->curnote.index + STARTMIDINOTE,
			normalize_velocity(str->curnote.volume), str->channel);
		#endif
		
		#ifdef DEBUGMIDI
		printf("chn%d note NEW=%d velocity=%d period=%d accuracy=%d\r\n",
			str->channel, str->curnote.index  + STARTMIDINOTE,
			normalize_velocity(str->curnote.volume),
			str->curnote.period, str->curnote.accuracy);
		#endif

		str->flags &= ~NOTE_NEW;
		str->flags &= ~NOTE_SILENCE;
	}
	if (str->flags & NOTE_NEWPITCH)
	{
		normalize_pitch(&tmppitch, str->curnote.bend);
		
		#ifdef REALMIDI	
		#ifdef ENABLE_BENDS	
		midiPitchBendOut(tmppitch.bendLSB, tmppitch.bendMSB, str->channel);
		#endif
		#endif
		
		#ifdef DEBUGMIDI
		#ifdef ENABLE_BENDS	
		printf("chn=%d PITCHNEW=%d MSB=%d LSB=%d\r\n",
			str->channel, str->curnote.bend,
			tmppitch.bendMSB, tmppitch.bendLSB);
		#endif
		#endif
		
		str->flags &= ~NOTE_NEWPITCH;
	}
}

void guitar_init()
{
	struna1.channel = CHANNEL1;
}
