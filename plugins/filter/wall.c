/*****************************************************************************
 * wall.c : Wall video plugin for vlc
 *****************************************************************************
 * Copyright (C) 2000, 2001 VideoLAN
 * $Id: wall.c,v 1.7 2002/01/05 03:49:18 sam Exp $
 *
 * Authors: Samuel Hocevar <sam@zoy.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111, USA.
 *****************************************************************************/

/*****************************************************************************
 * Preamble
 *****************************************************************************/
#include <errno.h>
#include <stdlib.h>                                      /* malloc(), free() */
#include <string.h>

#include <videolan/vlc.h>

#include "video.h"
#include "video_output.h"

#include "filter_common.h"

/*****************************************************************************
 * Capabilities defined in the other files.
 *****************************************************************************/
static void vout_getfunctions( function_list_t * p_function_list );

/*****************************************************************************
 * Build configuration tree.
 *****************************************************************************/
MODULE_CONFIG_START
MODULE_CONFIG_STOP

MODULE_INIT_START
    SET_DESCRIPTION( "image wall video module" )
    /* Capability score set to 0 because we don't want to be spawned
     * as a video output unless explicitly requested to */
    ADD_CAPABILITY( VOUT, 0 )
    ADD_SHORTCUT( "wall" )
MODULE_INIT_STOP

MODULE_ACTIVATE_START
    vout_getfunctions( &p_module->p_functions->vout );
MODULE_ACTIVATE_STOP

MODULE_DEACTIVATE_START
MODULE_DEACTIVATE_STOP

/*****************************************************************************
 * vout_sys_t: Wall video output method descriptor
 *****************************************************************************
 * This structure is part of the video output thread descriptor.
 * It describes the Wall specific properties of an output thread.
 *****************************************************************************/
typedef struct vout_sys_s
{
    int    i_col;
    int    i_row;
    int    i_vout;
    struct vout_list_s
    {
        int i_width;
        int i_height;
        struct vout_thread_s *p_vout;
    } *pp_vout;

} vout_sys_t;

/*****************************************************************************
 * Local prototypes
 *****************************************************************************/
static int  vout_Probe     ( probedata_t *p_data );
static int  vout_Create    ( vout_thread_t * );
static int  vout_Init      ( vout_thread_t * );
static void vout_End       ( vout_thread_t * );
static void vout_Destroy   ( vout_thread_t * );
static int  vout_Manage    ( vout_thread_t * );
static void vout_Render    ( vout_thread_t *, struct picture_s * );
static void vout_Display   ( vout_thread_t *, struct picture_s * );

static void RemoveAllVout  ( vout_thread_t *p_vout );

/*****************************************************************************
 * Functions exported as capabilities. They are declared as static so that
 * we don't pollute the namespace too much.
 *****************************************************************************/
static void vout_getfunctions( function_list_t * p_function_list )
{
    p_function_list->pf_probe = vout_Probe;
    p_function_list->functions.vout.pf_create     = vout_Create;
    p_function_list->functions.vout.pf_init       = vout_Init;
    p_function_list->functions.vout.pf_end        = vout_End;
    p_function_list->functions.vout.pf_destroy    = vout_Destroy;
    p_function_list->functions.vout.pf_manage     = vout_Manage;
    p_function_list->functions.vout.pf_render     = vout_Render;
    p_function_list->functions.vout.pf_display    = vout_Display;
}

/*****************************************************************************
 * intf_Probe: return a score
 *****************************************************************************/
static int vout_Probe( probedata_t *p_data )
{
    return( 0 );
}

/*****************************************************************************
 * vout_Create: allocates Wall video thread output method
 *****************************************************************************
 * This function allocates and initializes a Wall vout method.
 *****************************************************************************/
static int vout_Create( vout_thread_t *p_vout )
{
    /* Allocate structure */
    p_vout->p_sys = malloc( sizeof( vout_sys_t ) );
    if( p_vout->p_sys == NULL )
    {
        intf_ErrMsg("error: %s", strerror(ENOMEM) );
        return( 1 );
    }

    p_vout->p_sys->i_col = 3;
    p_vout->p_sys->i_row = 3;

    p_vout->p_sys->pp_vout = malloc( p_vout->p_sys->i_row *
                                     p_vout->p_sys->i_col *
                                     sizeof(struct vout_list_s) );
    if( p_vout->p_sys->pp_vout == NULL )
    {
        intf_ErrMsg("error: %s", strerror(ENOMEM) );
        free( p_vout->p_sys );
        return( 1 );
    }


    return( 0 );
}

/*****************************************************************************
 * vout_Init: initialize Wall video thread output method
 *****************************************************************************/
static int vout_Init( vout_thread_t *p_vout )
{
    int i_index, i_row, i_col, i_width, i_height;
    char *psz_filter;
    picture_t *p_pic;
    
    I_OUTPUTPICTURES = 0;

    /* Initialize the output structure */
    p_vout->output.i_chroma = p_vout->render.i_chroma;
    p_vout->output.i_width  = p_vout->render.i_width;
    p_vout->output.i_height = p_vout->render.i_height;
    p_vout->output.i_aspect = p_vout->render.i_aspect;

    /* Try to open the real video output */
    psz_filter = main_GetPszVariable( VOUT_FILTER_VAR, NULL );
    main_PutPszVariable( VOUT_FILTER_VAR, "" );

    intf_WarnMsg( 1, "filter: spawning the real video outputs" );

    p_vout->p_sys->i_vout = 0;

    /* FIXME: use bresenham instead of those ugly divisions */
    for( i_row = 0; i_row < p_vout->p_sys->i_row; i_row++ )
    {
        for( i_col = 0; i_col < p_vout->p_sys->i_col; i_col++ )
        {
            if( i_col + 1 < p_vout->p_sys->i_col )
            {
                i_width = ( p_vout->render.i_width
                             / p_vout->p_sys->i_col ) & ~0x1;
            }
            else
            {
                i_width = p_vout->render.i_width
                           - ( ( p_vout->render.i_width
                                  / p_vout->p_sys->i_col ) & ~0x1 ) * i_col;
            }

            if( i_row + 1 < p_vout->p_sys->i_row )
            {
                i_height = ( p_vout->render.i_height
                              / p_vout->p_sys->i_row ) & ~0x3;
            }
            else
            {
                i_height = p_vout->render.i_height
                            - ( ( p_vout->render.i_height
                                   / p_vout->p_sys->i_row ) & ~0x3 ) * i_row;
            }

            p_vout->p_sys->pp_vout[ p_vout->p_sys->i_vout ].p_vout =
                vout_CreateThread( NULL, i_width, i_height,
                                   p_vout->render.i_chroma,
                                   p_vout->render.i_aspect
                                    * p_vout->render.i_height / i_height
                                    * i_width / p_vout->render.i_width );
            if( p_vout->p_sys->pp_vout[ p_vout->p_sys->i_vout ].p_vout == NULL )
            {
                intf_ErrMsg( "vout error: failed to get %ix%i vout threads",
                             p_vout->p_sys->i_col, p_vout->p_sys->i_row );
                RemoveAllVout( p_vout );
                return 0;
            }

            p_vout->p_sys->i_vout++;
        }
    }

    main_PutPszVariable( VOUT_FILTER_VAR, psz_filter );

    ALLOCATE_DIRECTBUFFERS( VOUT_MAX_PICTURES );

    return( 0 );
}

/*****************************************************************************
 * vout_End: terminate Wall video thread output method
 *****************************************************************************/
static void vout_End( vout_thread_t *p_vout )
{
    int i_index;

    /* Free the fake output buffers we allocated */
    for( i_index = I_OUTPUTPICTURES ; i_index ; )
    {
        i_index--;
        free( PP_OUTPUTPICTURE[ i_index ]->p_data );
    }
}

/*****************************************************************************
 * vout_Destroy: destroy Wall video thread output method
 *****************************************************************************
 * Terminate an output method created by WallCreateOutputMethod
 *****************************************************************************/
static void vout_Destroy( vout_thread_t *p_vout )
{
    RemoveAllVout( p_vout );

    free( p_vout->p_sys->pp_vout );
    free( p_vout->p_sys );
}

/*****************************************************************************
 * vout_Manage: handle Wall events
 *****************************************************************************
 * This function should be called regularly by video output thread. It manages
 * console events. It returns a non null value on error.
 *****************************************************************************/
static int vout_Manage( vout_thread_t *p_vout )
{
    return( 0 );
}

/*****************************************************************************
 * vout_Render: displays previously rendered output
 *****************************************************************************
 * This function send the currently rendered image to Wall image, waits
 * until it is displayed and switch the two rendering buffers, preparing next
 * frame.
 *****************************************************************************/
static void vout_Render( vout_thread_t *p_vout, picture_t *p_pic )
{
    picture_t *p_outpic = NULL;
    int i_col, i_row, i_vout, i_plane;
    int pi_left_skip[VOUT_MAX_PLANES], pi_top_skip[VOUT_MAX_PLANES];

    i_vout = 0;

    for( i_plane = 0 ; i_plane < p_pic->i_planes ; i_plane++ )
    {
        pi_top_skip[i_plane] = 0;
    }

    for( i_row = 0; i_row < p_vout->p_sys->i_row; i_row++ )
    {
        for( i_plane = 0 ; i_plane < p_pic->i_planes ; i_plane++ )
        {
            pi_left_skip[i_plane] = 0;
        }

        for( i_col = 0; i_col < p_vout->p_sys->i_col; i_col++ )
        {
            while( ( p_outpic =
                vout_CreatePicture( p_vout->p_sys->pp_vout[ i_vout ].p_vout,
                                    0, 0, 0 )
                   ) == NULL )
            {
                if( p_vout->b_die || p_vout->b_error )
                {
                    vout_DestroyPicture(
                        p_vout->p_sys->pp_vout[ i_vout ].p_vout, p_outpic );
                    return;
                }

                msleep( VOUT_OUTMEM_SLEEP );
            }

            vout_DatePicture( p_vout->p_sys->pp_vout[ i_vout ].p_vout,
                              p_outpic, p_pic->date );
            vout_LinkPicture( p_vout->p_sys->pp_vout[ i_vout ].p_vout,
                              p_outpic );

            for( i_plane = 0 ; i_plane < p_pic->i_planes ; i_plane++ )
            {
                u8 *p_in, *p_in_end, *p_out;
                int i_in_pitch = p_pic->p[i_plane].i_pitch;
                int i_out_pitch = p_outpic->p[i_plane].i_pitch;

                p_in = p_pic->p[i_plane].p_pixels
                        + pi_top_skip[i_plane] + pi_left_skip[i_plane];

                p_in_end = p_in + p_outpic->p[i_plane].i_lines
                                   * p_pic->p[i_plane].i_pitch;

                p_out = p_outpic->p[i_plane].p_pixels;

                while( p_in < p_in_end )
                {
                    FAST_MEMCPY( p_out, p_in, i_out_pitch );
                    p_in += i_in_pitch;
                    p_out += i_out_pitch;
                }

                pi_left_skip[i_plane] += p_outpic->p[i_plane].i_pitch;
            }

            vout_UnlinkPicture( p_vout->p_sys->pp_vout[ i_vout ].p_vout,
                                p_outpic );
            vout_DisplayPicture( p_vout->p_sys->pp_vout[ i_vout ].p_vout,
                                 p_outpic );

            i_vout++;
        }

        for( i_plane = 0 ; i_plane < p_pic->i_planes ; i_plane++ )
        {
            pi_top_skip[i_plane] += p_outpic->p[i_plane].i_lines
                                     * p_pic->p[i_plane].i_pitch;
        }
    }
}

/*****************************************************************************
 * vout_Display: displays previously rendered output
 *****************************************************************************
 * This function send the currently rendered image to Invert image, waits
 * until it is displayed and switch the two rendering buffers, preparing next
 * frame.
 *****************************************************************************/
static void vout_Display( vout_thread_t *p_vout, picture_t *p_pic )
{
    ;
}

static void RemoveAllVout( vout_thread_t *p_vout )
{
    while( p_vout->p_sys->i_vout )
    {
         --p_vout->p_sys->i_vout;
         vout_DestroyThread(
             p_vout->p_sys->pp_vout[ p_vout->p_sys->i_vout ].p_vout, NULL );
    }
}

