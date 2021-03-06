#ifndef foovolumehfoo
#define foovolumehfoo

/***
  This file is part of PulseAudio.

  Copyright 2004-2006 Lennart Poettering
  Copyright 2006 Pierre Ossman <ossman@cendio.se> for Cendio AB

  PulseAudio is free software; you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License as published
  by the Free Software Foundation; either version 2.1 of the License,
  or (at your option) any later version.

  PulseAudio is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  General Public License for more details.

  You should have received a copy of the GNU Lesser General Public License
  along with PulseAudio; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
  USA.
***/

#include <inttypes.h>
#include <limits.h>

#include <pulse/cdecl.h>
#include <pulse/gccmacro.h>
#include <pulse/sample.h>
#include <pulse/channelmap.h>
#include <pulse/version.h>

/** \page volume Volume Control
 *
 * \section overv_sec Overview
 *
 * Sinks, sources, sink inputs and samples can all have their own volumes.
 * To deal with these, The PulseAudio libray contains a number of functions
 * that ease handling.
 *
 * The basic volume type in PulseAudio is the \ref pa_volume_t type. Most of
 * the time, applications will use the aggregated pa_cvolume structure that
 * can store the volume of all channels at once.
 *
 * Volumes commonly span between muted (0%), and normal (100%). It is possible
 * to set volumes to higher than 100%, but clipping might occur.
 *
 * \section calc_sec Calculations
 *
 * The volumes in PulseAudio are logarithmic in nature and applications
 * shouldn't perform calculations with them directly. Instead, they should
 * be converted to and from either dB or a linear scale:
 *
 * \li dB - pa_sw_volume_from_dB() / pa_sw_volume_to_dB()
 * \li Linear - pa_sw_volume_from_linear() / pa_sw_volume_to_linear()
 *
 * For simple multiplication, pa_sw_volume_multiply() and
 * pa_sw_cvolume_multiply() can be used.
 *
 * Calculations can only be reliably performed on software volumes
 * as it is commonly unknown what scale hardware volumes relate to.
 *
 * The functions described above are only valid when used with
 * software volumes. Hence it is usually a better idea to treat all
 * volume values as opaque with a range from PA_VOLUME_MUTED (0%) to
 * PA_VOLUME_NORM (100%) and to refrain from any calculations with
 * them.
 *
 * \section conv_sec Convenience Functions
 *
 * To handle the pa_cvolume structure, the PulseAudio library provides a
 * number of convenienc functions:
 *
 * \li pa_cvolume_valid() - Tests if a pa_cvolume structure is valid.
 * \li pa_cvolume_equal() - Tests if two pa_cvolume structures are identical.
 * \li pa_cvolume_channels_equal_to() - Tests if all channels of a pa_cvolume
 *                             structure have a given volume.
 * \li pa_cvolume_is_muted() - Tests if all channels of a pa_cvolume
 *                             structure are muted.
 * \li pa_cvolume_is_norm() - Tests if all channels of a pa_cvolume structure
 *                            are at a normal volume.
 * \li pa_cvolume_set() - Set all channels of a pa_cvolume structure to a
 *                        certain volume.
 * \li pa_cvolume_reset() - Set all channels of a pa_cvolume structure to a
 *                          normal volume.
 * \li pa_cvolume_mute() - Set all channels of a pa_cvolume structure to a
 *                         muted volume.
 * \li pa_cvolume_avg() - Return the average volume of all channels.
 * \li pa_cvolume_snprint() - Pretty print a pa_cvolume structure.
 */

/** \file
 * Constants and routines for volume handling */

PA_C_DECL_BEGIN

/** Volume specification:
 *  PA_VOLUME_MUTED: silence;
 * < PA_VOLUME_NORM: decreased volume;
 *   PA_VOLUME_NORM: normal volume;
 * > PA_VOLUME_NORM: increased volume */
typedef uint32_t pa_volume_t;

/** Normal volume (100%, 0 dB) */
#define PA_VOLUME_NORM ((pa_volume_t) 0x10000U)

/** Muted volume (0%, -inf dB) */
#define PA_VOLUME_MUTED ((pa_volume_t) 0U)

/** Maximum volume we can store. \since 0.9.15 */
#define PA_VOLUME_MAX ((pa_volume_t) UINT32_MAX)

/** A structure encapsulating a per-channel volume */
typedef struct pa_cvolume {
    uint8_t channels;                     /**< Number of channels */
    pa_volume_t values[PA_CHANNELS_MAX];  /**< Per-channel volume  */
} pa_cvolume;

/** Return non-zero when *a == *b */
int pa_cvolume_equal(const pa_cvolume *a, const pa_cvolume *b) PA_GCC_PURE;

/** Initialize the specified volume and return a pointer to
 * it. The sample spec will have a defined state but
 * pa_cvolume_valid() will fail for it. \since 0.9.13 */
pa_cvolume* pa_cvolume_init(pa_cvolume *a);

/** Set the volume of all channels to PA_VOLUME_NORM */
#define pa_cvolume_reset(a, n) pa_cvolume_set((a), (n), PA_VOLUME_NORM)

/** Set the volume of all channels to PA_VOLUME_MUTED */
#define pa_cvolume_mute(a, n) pa_cvolume_set((a), (n), PA_VOLUME_MUTED)

/** Set the volume of all channels to the specified parameter */
pa_cvolume* pa_cvolume_set(pa_cvolume *a, unsigned channels, pa_volume_t v);

/** Maximum length of the strings returned by
 * pa_cvolume_snprint(). Please note that this value can change with
 * any release without warning and without being considered API or ABI
 * breakage. You should not use this definition anywhere where it
 * might become part of an ABI.*/
#define PA_CVOLUME_SNPRINT_MAX 320

/** Pretty print a volume structure */
char *pa_cvolume_snprint(char *s, size_t l, const pa_cvolume *c);

/** Maximum length of the strings returned by
 * pa_cvolume_snprint_dB(). Please note that this value can change with
 * any release without warning and without being considered API or ABI
 * breakage. You should not use this definition anywhere where it
 * might become part of an ABI. \since 0.9.13 */
#define PA_SW_CVOLUME_SNPRINT_DB_MAX 448

/** Pretty print a volume structure but show dB values. \since 0.9.13 */
char *pa_sw_cvolume_snprint_dB(char *s, size_t l, const pa_cvolume *c);

/** Maximum length of the strings returned by
 * pa_volume_snprint(). Please note that this value can change with
 * any release without warning and without being considered API or ABI
 * breakage. You should not use this definition anywhere where it
 * might become part of an ABI. \since 0.9.15 */
#define PA_VOLUME_SNPRINT_MAX 10

/** Pretty print a volume \since 0.9.15 */
char *pa_volume_snprint(char *s, size_t l, pa_volume_t v);

/** Maximum length of the strings returned by
 * pa_volume_snprint_dB(). Please note that this value can change with
 * any release without warning and without being considered API or ABI
 * breakage. You should not use this definition anywhere where it
 * might become part of an ABI. \since 0.9.15 */
#define PA_SW_VOLUME_SNPRINT_DB_MAX 10

/** Pretty print a volume but show dB values. \since 0.9.15 */
char *pa_sw_volume_snprint_dB(char *s, size_t l, pa_volume_t v);

/** Return the average volume of all channels */
pa_volume_t pa_cvolume_avg(const pa_cvolume *a) PA_GCC_PURE;

/** Return the maximum volume of all channels. \since 0.9.12 */
pa_volume_t pa_cvolume_max(const pa_cvolume *a) PA_GCC_PURE;

/** Return TRUE when the passed cvolume structure is valid, FALSE otherwise */
int pa_cvolume_valid(const pa_cvolume *v) PA_GCC_PURE;

/** Return non-zero if the volume of all channels is equal to the specified value */
int pa_cvolume_channels_equal_to(const pa_cvolume *a, pa_volume_t v) PA_GCC_PURE;

/** Return 1 if the specified volume has all channels muted */
#define pa_cvolume_is_muted(a) pa_cvolume_channels_equal_to((a), PA_VOLUME_MUTED)

/** Return 1 if the specified volume has all channels on normal level */
#define pa_cvolume_is_norm(a) pa_cvolume_channels_equal_to((a), PA_VOLUME_NORM)

/** Multiply two volume specifications, return the result. This uses
 * PA_VOLUME_NORM as neutral element of multiplication. This is only
 * valid for software volumes! */
pa_volume_t pa_sw_volume_multiply(pa_volume_t a, pa_volume_t b) PA_GCC_CONST;

/** Multiply two per-channel volumes and return the result in
 * *dest. This is only valid for software volumes! */
pa_cvolume *pa_sw_cvolume_multiply(pa_cvolume *dest, const pa_cvolume *a, const pa_cvolume *b);

/** Divide two volume specifications, return the result. This uses
 * PA_VOLUME_NORM as neutral element of division. This is only valid
 * for software volumes! If a division by zero is tried the result
 * will be 0. \since 0.9.13 */
pa_volume_t pa_sw_volume_divide(pa_volume_t a, pa_volume_t b) PA_GCC_CONST;

/** Multiply to per-channel volumes and return the result in
 * *dest. This is only valid for software volumes! \since 0.9.13 */
pa_cvolume *pa_sw_cvolume_divide(pa_cvolume *dest, const pa_cvolume *a, const pa_cvolume *b);

/** Convert a decibel value to a volume. This is only valid for software volumes! */
pa_volume_t pa_sw_volume_from_dB(double f) PA_GCC_CONST;

/** Convert a volume to a decibel value. This is only valid for software volumes! */
double pa_sw_volume_to_dB(pa_volume_t v) PA_GCC_CONST;

/** Convert a linear factor to a volume. This is only valid for software volumes! */
pa_volume_t pa_sw_volume_from_linear(double v) PA_GCC_CONST;

/** Convert a volume to a linear factor. This is only valid for software volumes! */
double pa_sw_volume_to_linear(pa_volume_t v) PA_GCC_CONST;

#ifdef INFINITY
#define PA_DECIBEL_MININFTY ((double) -INFINITY)
#else
/** This value is used as minus infinity when using pa_volume_{to,from}_dB(). */
#define PA_DECIBEL_MININFTY ((double) -200.0)
#endif

/** Remap a volume from one channel mapping to a different channel mapping. \since 0.9.12 */
pa_cvolume *pa_cvolume_remap(pa_cvolume *v, const pa_channel_map *from, const pa_channel_map *to);

/** Return non-zero if the specified volume is compatible with the
 * specified sample spec. \since 0.9.13 */
int pa_cvolume_compatible(const pa_cvolume *v, const pa_sample_spec *ss) PA_GCC_PURE;

/** Return non-zero if the specified volume is compatible with the
 * specified sample spec. \since 0.9.15 */
int pa_cvolume_compatible_with_channel_map(const pa_cvolume *v, const pa_channel_map *cm) PA_GCC_PURE;

/** Calculate a 'balance' value for the specified volume with the
 * specified channel map. The return value will range from -1.0f
 * (left) to +1.0f (right). If no balance value is applicable to this
 * channel map the return value will always be 0.0f. See
 * pa_channel_map_can_balance(). \since 0.9.15 */
float pa_cvolume_get_balance(const pa_cvolume *v, const pa_channel_map *map) PA_GCC_PURE;

/** Adjust the 'balance' value for the specified volume with the
 * specified channel map. v will be modified in place and
 * returned. The balance is a value between -1.0f and +1.0f. This
 * operation might not be reversible! Also, after this call
 * pa_cvolume_get_balance() is not guaranteed to actually return the
 * requested balance value (e.g. when the input volume was zero anyway for
 * all channels). If no balance value is applicable to
 * this channel map the volume will not be modified. See
 * pa_channel_map_can_balance(). \since 0.9.15 */
pa_cvolume* pa_cvolume_set_balance(pa_cvolume *v, const pa_channel_map *map, float new_balance);

/** Calculate a 'fade' value (i.e. 'balance' between front and rear)
 * for the specified volume with the specified channel map. The return
 * value will range from -1.0f (rear) to +1.0f (left). If no fade
 * value is applicable to this channel map the return value will
 * always be 0.0f. See pa_channel_map_can_fade(). \since 0.9.15 */
float pa_cvolume_get_fade(const pa_cvolume *v, const pa_channel_map *map) PA_GCC_PURE;

/** Adjust the 'fade' value (i.e. 'balance' between front and rear)
 * for the specified volume with the specified channel map. v will be
 * modified in place and returned. The balance is a value between
 * -1.0f and +1.0f. This operation might not be reversible! Also,
 * after this call pa_cvolume_get_fade() is not guaranteed to actually
 * return the requested fade value (e.g. when the input volume was
 * zero anyway for all channels). If no fade value is applicable to
 * this channel map the volume will not be modified. See
 * pa_channel_map_can_fade(). \since 0.9.15 */
pa_cvolume* pa_cvolume_set_fade(pa_cvolume *v, const pa_channel_map *map, float new_fade);

/** Scale the passed pa_cvolume structure so that the maximum volume
 * of all channels equals max. The proportions between the channel
 * volumes are kept. \since 0.9.15 */
pa_cvolume* pa_cvolume_scale(pa_cvolume *v, pa_volume_t max);

PA_C_DECL_END

#endif
