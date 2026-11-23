/*
 * Copyright (C) 2013 Robert Kooima
 *
 * NEVERBALL is  free software; you can redistribute  it and/or modify
 * it under the  terms of the GNU General  Public License as published
 * by the Free  Software Foundation; either version 2  of the License,
 * or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT  ANY  WARRANTY;  without   even  the  implied  warranty  of
 * MERCHANTABILITY or  FITNESS FOR A PARTICULAR PURPOSE.   See the GNU
 * General Public License for more details.
 */

#ifndef HMD_H
#define HMD_H

/*---------------------------------------------------------------------------*/

int  hmd_stat(void);

void hmd_init(void);
void hmd_free(void);
void hmd_step(void);
void hmd_swap(void);

void hmd_prep_left(void);
void hmd_prep_right(void);

void hmd_persp(float, float);
void hmd_ortho(void);

/*
 * Return non-zero if the headset tracks the real floor. If F is given,
 * fill it with the transform that takes the view hmd_persp loads (rooted
 * at the starting head pose) to one rooted on the floor.
 */
int  hmd_floor(float *F);

/*
 * Play hand for Life-size play: G is the controller's aim pose on the
 * floor in metres, v and w its linear and angular velocity, and live
 * whether the trigger is held. Return zero if the hand is not tracked.
 */
int  hmd_hand(float G[16], float v[3], float w[3], int *live);

/*---------------------------------------------------------------------------*/

#endif
