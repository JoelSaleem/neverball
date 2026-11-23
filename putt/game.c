/*
 * Copyright (C) 2003 Robert Kooima
 *
 * NEVERPUTT is  free software; you can redistribute  it and/or modify
 * it under the  terms of the GNU General  Public License as published
 * by the Free  Software Foundation; either version 2  of the License,
 * or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT  ANY  WARRANTY;  without   even  the  implied  warranty  of
 * MERCHANTABILITY or  FITNESS FOR A PARTICULAR PURPOSE.   See the GNU
 * General Public License for more details.
 */

#include <SDL.h>
#include <math.h>

#include "glext.h"
#include "game.h"
#include "vec3.h"
#include "geom.h"
#include "ball.h"
#include "hole.h"
#include "hud.h"
#include "hmd.h"
#include "log.h"
#include "image.h"
#include "audio.h"
#include "config.h"
#include "video.h"

#include "solid_draw.h"
#include "solid_sim.h"
#include "solid_all.h"

#ifdef __ANDROID__
#include <stdio.h>
#include <string.h>
#include <sys/system_properties.h>
#endif

/*---------------------------------------------------------------------------*/

static struct s_full file;
static int           ball;

static int state;

static float view_a;                    /* Ideal view rotation about Y axis  */
static float view_m;
static float view_ry;                   /* Angular velocity about Y axis     */
static float view_dy;                   /* Ideal view distance above ball    */
static float view_dz;                   /* Ideal view distance behind ball   */

static float view_c[3];                 /* Current view center               */
static float view_v[3];                 /* Current view vector               */
static float view_p[3];                 /* Current view position             */
static float view_e[3][3];              /* Current view orientation          */

static float jump_e = 1;                /* Jumping enabled flag              */
static float jump_b = 0;                /* Jump-in-progress flag             */
static float jump_dt;                   /* Jump duration                     */
static float jump_p[3];                 /* Jump destination                  */

static float idle_t;                    /* Idling timeout                    */

/* Life-size play: the ball is a golf ball and the player swings a Putter. */

#define PUTT_SCALE    0.341f            /* Metres per unit                   */
#define PUTTER_LENGTH 0.85f             /* Hand to head, metres              */
#define PUTTER_REACH  0.035f            /* Contact radius of the head        */
#define PUTTER_GAIN   1.3f              /* Ball speed per head speed         */
#define PUTTER_MAX    6.0f              /* Ball speed cap, metres/second     */
#define STANCE        0.35f             /* Distance beside the ball, metres  */
#define BLINK_TIME    0.2f

static float stand[3];                  /* Player position                   */
static int   stand_e;                   /* Player position set flag          */
static float blink_t;                   /* Blink-follow time remaining       */

static float putter_G[16];              /* Play hand pose on the floor       */
static int   putter_e;                  /* Play hand tracked flag            */
static int   putter_live;               /* Putter Live flag                  */

static float swing_v[3];                /* Ball velocity of a Swing          */
static int   swing_e;                   /* Swing pending flag                */

/*---------------------------------------------------------------------------*/

static void view_init(void)
{
    view_a  = 0.f;
    view_m  = 0.f;
    view_ry = 0.f;
    view_dy = 3.f;
    view_dz = 5.f;

    view_c[0] = 0.f;
    view_c[1] = 0.f;
    view_c[2] = 0.f;

    view_p[0] =     0.f;
    view_p[1] = view_dy;
    view_p[2] = view_dz;

    view_e[0][0] = 1.f;
    view_e[0][1] = 0.f;
    view_e[0][2] = 0.f;
    view_e[1][0] = 0.f;
    view_e[1][1] = 1.f;
    view_e[1][2] = 0.f;
    view_e[2][0] = 0.f;
    view_e[2][1] = 0.f;
    view_e[2][2] = 1.f;
}

int putt_init(const char *s)
{
    stand_e = 0;
    swing_e = 0;

    int i;

    jump_e = 1;
    jump_b = 0;

    idle_t = 1.0f;

    view_init();

    if (!(state = sol_load_full(&file, s, config_get_d(CONFIG_SHADOW))))
        return 0;

    sol_init_sim(&file.vary);

    for (i = 0; i < file.base.dc; i++)
    {
        const char *k = file.base.av + file.base.dv[i].ai;
        const char *v = file.base.av + file.base.dv[i].aj;

        if (strcmp(k, "idle") == 0)
        {
            sscanf(v, "%f", &idle_t);

            if (idle_t < 1.0f)
                idle_t = 1.0f;
        }
    }
    return 1;
}

void putt_free(void)
{
    sol_quit_sim();
    sol_free_full(&file);
}

void putt_free_objects(void)
{
    if (state)
        sol_free_draw(&file.draw);
}

void putt_load_objects(void)
{
    if (state)
        sol_load_draw(&file.draw, &file.vary, config_get_d(CONFIG_SHADOW));
}

/*---------------------------------------------------------------------------*/

static void game_draw_vect(struct s_rend *rend, const struct s_vary *fp)
{
    if (view_m > 0.f)
    {
        glPushMatrix();
        {
            glTranslatef(fp->uv[ball].p[0],
                         fp->uv[ball].p[1],
                         fp->uv[ball].p[2]);
            glRotatef(view_a, 0.0f, 1.0f, 0.0f);
            glScalef(fp->uv[ball].r,
                     fp->uv[ball].r * 0.1f, view_m);

            vect_draw(rend);
        }
        glPopMatrix();
    }
}

static void game_draw_balls(struct s_rend *rend,
                            const struct s_vary *fp,
                            const float *bill_M, float t)
{
    static const GLfloat color[5][4] = {
        { 1.0f, 1.0f, 1.0f, 0.7f },
        { 1.0f, 0.0f, 0.0f, 1.0f },
        { 0.0f, 1.0f, 0.0f, 1.0f },
        { 0.0f, 0.0f, 1.0f, 1.0f },
        { 1.0f, 1.0f, 0.0f, 1.0f },
    };

    int ui;

    r_color_mtrl(rend, 1);

    for (ui = curr_party(); ui > 0; ui--)
    {
        if (ui == ball)
        {
            float ball_M[16];
            float pend_M[16];

            m_basis(ball_M, fp->uv[ui].e[0], fp->uv[ui].e[1], fp->uv[ui].e[2]);
            m_basis(pend_M, fp->uv[ui].E[0], fp->uv[ui].E[1], fp->uv[ui].E[2]);

            glPushMatrix();
            {
                glTranslatef(fp->uv[ui].p[0],
                             fp->uv[ui].p[1] + BALL_FUDGE,
                             fp->uv[ui].p[2]);
                glScalef(fp->uv[ui].r,
                         fp->uv[ui].r,
                         fp->uv[ui].r);

                glColor4f(color[ui][0],
                          color[ui][1],
                          color[ui][2],
                          color[ui][3]);
                ball_draw(rend, ball_M, pend_M, bill_M, t);
            }
            glPopMatrix();
        }
        else
        {
            glPushMatrix();
            {
                glTranslatef(fp->uv[ui].p[0],
                             fp->uv[ui].p[1] - fp->uv[ui].r + BALL_FUDGE,
                             fp->uv[ui].p[2]);
                glScalef(fp->uv[ui].r,
                         fp->uv[ui].r,
                         fp->uv[ui].r);

                glColor4f(color[ui][0],
                          color[ui][1],
                          color[ui][2], 0.5f);

                mark_draw(rend);
            }
            glPopMatrix();
        }
    }

    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    r_color_mtrl(rend, 0);
}

static void game_draw_flags(struct s_rend *rend, const struct s_base *fp)
{
    int zi;

    for (zi = 0; zi < fp->zc; zi++)
        flag_draw(rend, fp->zv[zi].p);
}

static void game_draw_beams(struct s_rend *rend, const struct s_base *bp,
                                                 const struct s_vary *vp)
{
    static const GLfloat jump_c[2][4]    =  {{ 0.7f, 0.5f, 1.0f, 0.5f },
                                             { 0.7f, 0.5f, 1.0f, 0.8f }};
    static const GLfloat swch_c[2][2][4] = {{{ 1.0f, 0.0f, 0.0f, 0.5f },
                                             { 1.0f, 0.0f, 0.0f, 0.8f }},
                                            {{ 0.0f, 1.0f, 0.0f, 0.5f },
                                             { 0.0f, 1.0f, 0.0f, 0.8f }}};

    int i;

    /* Jump beams */

    for (i = 0; i < bp->jc; i++)
        beam_draw(rend, bp->jv[i].p, jump_c[jump_e ? 0 : 1],
                        bp->jv[i].r, 2.0f);

    /* Switch beams */

    for (i = 0; i < vp->xc; i++)
    {
        struct v_swch *xp = vp->xv + i;

        if (!xp->base->i)
            beam_draw(rend, xp->base->p, swch_c[xp->f][xp->e],
                            xp->base->r, 2.0f);
    }
}

/*---------------------------------------------------------------------------*/

static void game_shadow_conf(int enable)
{
    if (enable && config_get_d(CONFIG_SHADOW))
    {
        tex_env_select(&tex_env_shadow_clip,
                       &tex_env_shadow,
                       &tex_env_default,
                       NULL);
    }
    else
    {
        tex_env_active(&tex_env_default);
    }
}

/* Draw a box from its centre and three half-extent axes. */

static void draw_box(const float c[3], const float x[3],
                     const float y[3], const float z[3])
{
    static const int faces[6][4] = {
        { 1, 3, 7, 5 }, { 0, 4, 6, 2 }, { 2, 6, 7, 3 },
        { 0, 1, 5, 4 }, { 4, 5, 7, 6 }, { 0, 2, 3, 1 },
    };
    int i, j;

    glBegin(GL_QUADS);
    for (i = 0; i < 6; i++)
        for (j = 0; j < 4; j++)
        {
            const int k = faces[i][j];
            const float sx = (k & 1) ? 1.0f : -1.0f;
            const float sy = (k & 2) ? 1.0f : -1.0f;
            const float sz = (k & 4) ? 1.0f : -1.0f;

            glVertex3f(c[0] + sx * x[0] + sy * y[0] + sz * z[0],
                       c[1] + sx * x[1] + sy * y[1] + sz * z[1],
                       c[2] + sx * x[2] + sy * y[2] + sz * z[2]);
        }
    glEnd();
}

/* Draw the Putter in floor coordinates, a ghost unless it is Live. */

static void putter_draw(void)
{
    const float *X = putter_G + 0, *Y = putter_G + 4, *Z = putter_G + 8;
    const float *p = putter_G + 12;
    const float a = putter_live ? 1.0f : 0.35f;
    float c[3], x[3], y[3], z[3];

    if (!putter_e)
        return;

    glPushAttrib(GL_ENABLE_BIT | GL_CURRENT_BIT);
    {
        glDisable(GL_TEXTURE_2D);
        glDisable(GL_LIGHTING);
        glEnable(GL_BLEND);

        /* Shaft, down the controller's pointing axis. */

        v_mad(c, p, Z, -0.5f * PUTTER_LENGTH);
        v_scl(x, X, 0.008f);
        v_scl(y, Y, 0.008f);
        v_scl(z, Z, 0.5f * PUTTER_LENGTH);
        glColor4f(0.75f, 0.75f, 0.8f, a);
        draw_box(c, x, y, z);

        /* Head. */

        v_mad(c, p, Z, -PUTTER_LENGTH);
        v_scl(x, X, 0.05f);
        v_scl(y, Y, 0.015f);
        v_scl(z, Z, 0.015f);
        glColor4f(0.2f, 0.2f, 0.25f, a);
        draw_box(c, x, y, z);
    }
    glPopAttrib();
}

void putt_draw(int pose, float t)
{
    const float light_p[4] = { 8.f, 32.f, 8.f, 0.f };

    struct s_draw *fp = &file.draw;
    struct s_rend rend;

    float fov = FOV;
    float F[16];
    int   life;

    if (!state)
        return;

    /* Blink-follow shows nothing while the player is moved. */

    if ((life = stand_e && hmd_floor(F)) && blink_t > 0.0f)
        return;

    fp->shadow_ui = ball;

    game_shadow_conf(1);
    r_draw_enable(&rend);

    if (jump_b) fov *= 2.0f * fabsf(jump_dt - 0.5f);

    video_push_persp(fov, 0.1f, FAR_DIST);
    glPushMatrix();
    {
        float T[16], M[16], v[3], c[3];

        /* In VR, move the view center up to keep the viewer level. */

        v_cpy(c, view_c);

        if (hmd_stat())
            c[1] += view_dy;

        video_calc_view(T, c, view_p, view_e[1]);
        m_xps(M, T);

        v_sub(v, c, view_p);

        if (life)
        {
            glMultMatrixf(F);
            glPushMatrix();
            glScalef(PUTT_SCALE, PUTT_SCALE, PUTT_SCALE);
            glTranslatef(-stand[0], -stand[1], -stand[2]);
        }
        else
        {
            glTranslatef(0.f, 0.f, -v_len(v));
            glMultMatrixf(M);
            glTranslatef(-c[0], -c[1], -c[2]);
        }

        /* Center the skybox about the position of the camera. */

        glPushMatrix();
        {
            if (life)
                glTranslatef(stand[0], stand[1], stand[2]);
            else
                glTranslatef(view_p[0], view_p[1], view_p[2]);
            back_draw(&rend);
        }
        glPopMatrix();

        glEnable(GL_LIGHT0);
        glLightfv(GL_LIGHT0, GL_POSITION, light_p);

        /* Draw the floor. */

        sol_draw(fp, &rend, 0, 1);

        /* Draw the game elements. */

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        if (pose == 0)
        {
            game_draw_balls(&rend, fp->vary, T, t);
            game_draw_vect(&rend, fp->vary);
        }

        glDepthMask(GL_FALSE);
        {
            game_draw_flags(&rend, fp->base);
            game_draw_beams(&rend, fp->base, fp->vary);
        }
        glDepthMask(GL_TRUE);

        glColor4f(1.0f, 1.0f, 1.0f, 1.0f);

        if (life)
        {
            glPopMatrix();
            putter_draw();
        }
    }
    glPopMatrix();
    video_pop_matrix();

    r_draw_disable(&rend);
    game_shadow_conf(0);
}

/*---------------------------------------------------------------------------*/

void putt_update_view(float dt)
{
    const float y[3] = { 0.f, 1.f, 0.f };

    float dy;
    float dz;
    float k;
    float e[3];
    float d[3];
    float s = 2.f * dt;

    if (!state)
        return;

    /* Center the view about the ball. */

    v_cpy(view_c, file.vary.uv[ball].p);
    v_inv(view_v, file.vary.uv[ball].v);

    switch (config_get_d(CONFIG_CAMERA))
    {
    case 2:
        /* Camera 2: View vector is given by view angle. */

        view_e[2][0] = fsinf(V_RAD(view_a));
        view_e[2][1] = 0.f;
        view_e[2][2] = fcosf(V_RAD(view_a));

        s = 1.f;
        break;

    default:
        /* View vector approaches the ball velocity vector. */

        v_mad(e, view_v, y, v_dot(view_v, y));
        v_inv(e, e);

        k = v_dot(view_v, view_v);

        v_sub(view_e[2], view_p, view_c);
        v_mad(view_e[2], view_e[2], view_v, k * dt * 0.1f);
    }

    /* Orthonormalize the basis of the view in its new position. */

    v_crs(view_e[0], view_e[1], view_e[2]);
    v_crs(view_e[2], view_e[0], view_e[1]);
    v_nrm(view_e[0], view_e[0]);
    v_nrm(view_e[2], view_e[2]);

    /* The current view (dy, dz) approaches the ideal (view_dy, view_dz). */

    v_sub(d, view_p, view_c);

    dy = v_dot(view_e[1], d);
    dz = v_dot(view_e[2], d);

    dy += (view_dy - dy) * s;
    dz += (view_dz - dz) * s;

    /* Compute the new view position. */

    view_p[0] = view_p[1] = view_p[2] = 0.f;

    v_mad(view_p, view_c, view_e[1], dy);
    v_mad(view_p, view_p, view_e[2], dz);

    view_a = V_DEG(fatan2f(view_e[2][0], view_e[2][2]));
}

static int game_update_state(float dt)
{
    static float t = 0.f;

    struct s_vary *fp = &file.vary;
    float p[3];

    if (dt > 0.f)
        t += dt;
    else
        t = 0.f;

    /* Test for a switch. */

    if (sol_swch_test(fp, NULL, ball) == SWCH_INSIDE)
        audio_play(AUD_SWITCH, 1.f);

    /* Test for a jump. */

    if (jump_e == 1 && jump_b == 0 && (sol_jump_test(fp, jump_p, ball) ==
                                       JUMP_INSIDE))
    {
        jump_b  = 1;
        jump_e  = 0;
        jump_dt = 0.f;

        audio_play(AUD_JUMP, 1.f);
    }
    if (jump_e == 0 && jump_b == 0 && (sol_jump_test(fp, jump_p, ball) ==
                                       JUMP_OUTSIDE))
    {
        jump_e = 1;
    }

    /* Test for fall-out. */

    if (file.base.vc == 0 || fp->uv[ball].p[1] < file.base.vv[0].p[1])
        return PUTT_FALL;

    /* Test for a goal or stop. */

    if (t > 1.f && sol_goal_test(fp, p, ball))
    {
        t = 0.f;
        return PUTT_GOAL;
    }

    if (t > idle_t)
    {
        t = 0.f;
        return PUTT_STOP;
    }

    return PUTT_NONE;
}

/*
 * On  most  hardware, rendering  requires  much  more  computing power  than
 * physics.  Since  physics takes less time  than graphics, it  make sense to
 * detach  the physics update  time step  from the  graphics frame  rate.  By
 * performing multiple physics updates for  each graphics update, we get away
 * with higher quality physics with little impact on overall performance.
 *
 * Toward this  end, we establish a  baseline maximum physics  time step.  If
 * the measured  frame time  exceeds this  maximum, we cut  the time  step in
 * half, and  do two updates.  If THIS  time step exceeds the  maximum, we do
 * four updates.  And  so on.  In this way, the physics  system is allowed to
 * seek an optimal update rate independent of, yet in integral sync with, the
 * graphics frame rate.
 */

int putt_step(const float g[3], float dt)
{
    struct s_vary *fp = &file.vary;

    static float s = 0.f;
    static float t = 0.f;

    float d = 0.f;
    float b = 0.f;
    float st = 0.f;
    int i, n = 1, m = 0;

    if (!state)
        return PUTT_NONE;

    s = (7.f * s + dt) / 8.f;
    t = s;

    if (jump_b)
    {
        jump_dt += dt;

        /* Handle a jump. */

        if (0.5f < jump_dt)
        {
            fp->uv[ball].p[0] = jump_p[0];
            fp->uv[ball].p[1] = jump_p[1];
            fp->uv[ball].p[2] = jump_p[2];
        }
        if (1.f < jump_dt)
            jump_b = 0;
    }
    else
    {
        /* Run the sim. */

        while (t > MAX_DT && n < MAX_DN)
        {
            t /= 2;
            n *= 2;
        }

        for (i = 0; i < n; i++)
        {
            d = sol_step(fp, NULL, g, t, ball, &m);

            if (b < d)
                b = d;
            if (m)
                st += t;
        }

        /* Mix the sound of a ball bounce. */

        if (b > 0.5f)
            audio_play(AUD_BUMP, (b - 0.5f) * 2.0f);
    }

    putt_update_view(dt);
    return game_update_state(st);
}

void putt_putt(void)
{
    if (swing_e)
    {
        file.vary.uv[ball].v[0] = swing_v[0];
        file.vary.uv[ball].v[1] = swing_v[1] + BALL_FUDGE;
        file.vary.uv[ball].v[2] = swing_v[2];

        swing_e = 0;
        view_m  = 0.f;
        return;
    }

    /*
     * HACK: The BALL_FUDGE here  guarantees that a putt doesn't drive
     * the ball  too directly down  toward a lump,  triggering rolling
     * friction too early and stopping the ball prematurely.
     */

    file.vary.uv[ball].v[0] = -4.f * view_e[2][0] * view_m;
    file.vary.uv[ball].v[1] = -4.f * view_e[2][1] * view_m + BALL_FUDGE;
    file.vary.uv[ball].v[2] = -4.f * view_e[2][2] * view_m;

    view_m = 0.f;
}

/*---------------------------------------------------------------------------*/

/*
 * Blink-follow for a new Stroke: stand beside the stopped ball, square to
 * the line to the hole with the hole on the player's left.
 */
void putt_stand(void)
{
    const float *p = file.vary.uv[ball].p;
    float d[3] = { 0.0f, 0.0f, -1.0f };

    if (!hmd_floor(NULL))
        return;

    if (file.base.zc)
    {
        v_sub(d, file.base.zv[0].p, p);
        d[1] = 0.0f;
    }
    if (v_len(d) < 0.001f)
    {
        d[0] = 0.0f;
        d[2] = -1.0f;
    }
    v_nrm(d, d);

    stand[0] = p[0] + d[2] * STANCE / PUTT_SCALE;
    stand[1] = p[1] - file.vary.uv[ball].r;
    stand[2] = p[2] - d[0] * STANCE / PUTT_SCALE;

    stand_e = 1;
    blink_t = BLINK_TIME;

    log_printf("Stand beside ball at %.1f %.1f %.1f\n", p[0], p[1], p[2]);
}

/*
 * Track the Putter. Return non-zero when a Live head hits the ball, with
 * the Stroke saved for putt_putt.
 */
int putt_putter(float dt)
{
    float v[3], w[3], r[3], h[3], hv[3], b[3], t[3], s;
    const float *Z = putter_G + 8, *p = putter_G + 12;

    if (blink_t > 0.0f)
    {
        blink_t -= dt;
        return 0;
    }

#ifdef __ANDROID__
    /* Testing without controllers: `setprop debug.neverball.swing "<id> <vx> <vz>"` m/s. */
    {
        static char last[PROP_VALUE_MAX];
        char value[PROP_VALUE_MAX];
        float vx, vz;

        if (stand_e && __system_property_get("debug.neverball.swing", value) > 0 &&
            strcmp(value, last) != 0 && sscanf(value, "%*s %f %f", &vx, &vz) == 2)
        {
            SDL_strlcpy(last, value, sizeof (last));
            swing_v[0] = vx / PUTT_SCALE;
            swing_v[1] = 0.0f;
            swing_v[2] = vz / PUTT_SCALE;
            swing_e = 1;
            log_printf("Test Stroke %.1f %.1f m/s\n", vx, vz);
            return 1;
        }
    }
#endif

    if (!stand_e || !(putter_e = hmd_hand(putter_G, v, w, &putter_live)))
        return 0;

    if (!putter_live)
        return 0;

    /* Head position and velocity on the floor. */

    v_mad(h, p, Z, -PUTTER_LENGTH);
    v_sub(r, h, p);
    v_crs(hv, w, r);
    v_add(hv, hv, v);
    hv[1] = 0.0f;

    /* Ball position on the floor. */

    v_sub(b, file.vary.uv[ball].p, stand);
    v_scl(b, b, PUTT_SCALE);
    v_sub(t, b, h);

    if (v_len(t) > PUTTER_REACH + file.vary.uv[ball].r * PUTT_SCALE ||
        v_dot(hv, t) <= 0.0f || (s = v_len(hv)) < 0.1f)
        return 0;

    s = MIN(s * PUTTER_GAIN, PUTTER_MAX) / PUTT_SCALE;

    v_nrm(hv, hv);
    v_scl(swing_v, hv, s);
    swing_e = 1;

    log_printf("Stroke at %.2f m/s\n", s * PUTT_SCALE);

    return 1;
}

void putt_set_rot(int d)
{
    view_a += (float) (30.f * d) / config_get_d(CONFIG_MOUSE_SENSE);
}

void putt_clr_mag(void)
{
    view_m = 1.f;
}

void putt_set_mag(int d)
{
    view_m -= (float) (1.f * d) / config_get_d(CONFIG_MOUSE_SENSE);

    if (view_m < 0.25f)
        view_m = 0.25f;
}

void putt_set_fly(float k)
{
    struct s_vary *fp = &file.vary;

    float  x[3] = { 1.f, 0.f, 0.f };
    float  y[3] = { 0.f, 1.f, 0.f };
    float  z[3] = { 0.f, 0.f, 1.f };
    float c0[3] = { 0.f, 0.f, 0.f };
    float p0[3] = { 0.f, 0.f, 0.f };
    float c1[3] = { 0.f, 0.f, 0.f };
    float p1[3] = { 0.f, 0.f, 0.f };
    float  v[3];

    if (!state)
        return;

    v_cpy(view_e[0], x);
    v_cpy(view_e[1], y);

    if (fp->base->zc > 0)
        v_sub(view_e[2], fp->uv[ball].p, fp->base->zv[0].p);
    else
        v_cpy(view_e[2], z);

    if (fabs(v_dot(view_e[1], view_e[2])) > 0.999)
        v_cpy(view_e[2], z);

    v_crs(view_e[0], view_e[1], view_e[2]);
    v_crs(view_e[2], view_e[0], view_e[1]);

    v_nrm(view_e[0], view_e[0]);
    v_nrm(view_e[2], view_e[2]);

    /* k = 0.0 view is at the ball. */

    if (fp->uc > 0)
    {
        v_cpy(c0, fp->uv[ball].p);
        v_cpy(p0, fp->uv[ball].p);
    }

    v_mad(p0, p0, view_e[1], view_dy);
    v_mad(p0, p0, view_e[2], view_dz);

    /* k = +1.0 view is s_view 0 */

    if (k >= 0 && fp->base->wc > 0)
    {
        v_cpy(p1, fp->base->wv[0].p);
        v_cpy(c1, fp->base->wv[0].q);
    }

    /* k = -1.0 view is s_view 1 */

    if (k <= 0 && fp->base->wc > 1)
    {
        v_cpy(p1, fp->base->wv[1].p);
        v_cpy(c1, fp->base->wv[1].q);
    }

    /* Interpolate the views. */

    v_sub(v, p1, p0);
    v_mad(view_p, p0, v, k * k);

    v_sub(v, c1, c0);
    v_mad(view_c, c0, v, k * k);

    /* Orthonormalize the view basis. */

    v_sub(view_e[2], view_p, view_c);
    v_crs(view_e[0], view_e[1], view_e[2]);
    v_crs(view_e[2], view_e[0], view_e[1]);
    v_nrm(view_e[0], view_e[0]);
    v_nrm(view_e[2], view_e[2]);

    view_a = V_DEG(fatan2f(view_e[2][0], view_e[2][2]));
}

void putt_ball(int i)
{
    int ui;

    ball = i;

    jump_e = 1;
    jump_b = 0;

    for (ui = 0; ui < file.vary.uc; ui++)
    {
        file.vary.uv[ui].v[0] = 0.f;
        file.vary.uv[ui].v[1] = 0.f;
        file.vary.uv[ui].v[2] = 0.f;

        file.vary.uv[ui].w[0] = 0.f;
        file.vary.uv[ui].w[1] = 0.f;
        file.vary.uv[ui].w[2] = 0.f;
    }
}

void putt_get_pos(float p[3], float e[3][3])
{
    v_cpy(p,    file.vary.uv[ball].p);
    v_cpy(e[0], file.vary.uv[ball].e[0]);
    v_cpy(e[1], file.vary.uv[ball].e[1]);
    v_cpy(e[2], file.vary.uv[ball].e[2]);
}

void putt_set_pos(float p[3], float e[3][3])
{
    v_cpy(file.vary.uv[ball].p,    p);
    v_cpy(file.vary.uv[ball].e[0], e[0]);
    v_cpy(file.vary.uv[ball].e[1], e[1]);
    v_cpy(file.vary.uv[ball].e[2], e[2]);
}

/*---------------------------------------------------------------------------*/

