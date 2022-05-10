/* -*- c-basic-offset: 4; indent-tabs-mode: nil -*- */
/* ====================================================================
 * Copyright (c) 2006 Carnegie Mellon University.  All rights 
 * reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer. 
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * This work was supported in part by funding from the Defense Advanced 
 * Research Projects Agency and the National Science Foundation of the 
 * United States of America, and the CMU Sphinx Speech Consortium.
 *
 * THIS SOFTWARE IS PROVIDED BY CARNEGIE MELLON UNIVERSITY ``AS IS'' AND 
 * ANY EXPRESSED OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, 
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL CARNEGIE MELLON UNIVERSITY
 * NOR ITS EMPLOYEES BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT 
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, 
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY 
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT 
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE 
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * ====================================================================
 *
 */
/*********************************************************************
 *
 * File: fe_warp.c
 * 
 * Description: 
 *      Allows a caller to choose a warping function.
 *********************************************************************/

/* static char rcsid[] = "@(#)$Id: fe_warp.c,v 1.2 2006/02/17 00:31:34 egouvea Exp $";*/
#ifndef FE_WARP_H
#define FE_WARP_H

#include "fe_internal.h"
#include "_fe_warp_inverse_linear.h"
#include "_fe_warp_affine.h"
#include "_fe_warp_piecewise_linear.h"
//#include "fe_warp.h"

//#include "xyzsphinxbase/err.h"
#include <xyzsphinxbase/err.h>

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <math.h>
#include <functional>



// #ifdef __cplusplus
// extern "C" {
// #endif
// #if 0
// /* Fool Emacs. */
// }
// #endif


// #define FE_WARP_ID_INVERSE_LINEAR	0
// #define FE_WARP_ID_AFFINE	        1
// #define FE_WARP_ID_PIECEWISE_LINEAR	2
#define FE_WARP_ID_EIDE_GISH		3
#define FE_WARP_ID_MAX		        2
#define FE_WARP_ID_NONE	       0xffffffff

// In C:
// typedef struct {
//             void (*set_parameters)(char const *param_str, float sampling_rate);
//             const char * (*doc)(void);
//             uint32 (*id)(void);
//             uint32 (*n_param)(void);
//             float (*warped_to_unwarped)(float nonlinear);
//             float (*unwarped_to_warped)(float linear);
//             void (*print)(const char *label);
//         } fe_warp_conf_t;

// In C++ (madness)
typedef struct {
            std::function<void(const char*, float)> set_parameters;
            std::function<const char*(void)> doc;
            std::function<uint32(void)> id;
            std::function<uint32(void)> n_param;
            std::function<float(float)> warped_to_unwarped;
            std::function<float(float)> unwarped_to_warped;
            std::function<void(const char*)> print;
        } fe_warp_conf_t;

class XYZ_SB_FE_Warp {

    private:

        

        /* This is for aliases for each of the entries below. Currently not
        used.
        */
        //static char *__name2id[] = { //In C
        const char *__name2id[4] = {//in C++
            "inverse",
            "linear",
            "piecewise",
            NULL
        };

        //static char *name2id[] = { //In C
        const char *name2id[4] = { //In C++
            "inverse_linear",
            "affine",
            "piecewise_linear",
            NULL
        };

        XYZ_SB_FE_Warp_inverse_linear _warp_inv_lin;
        XYZ_SB_FE_Warp_affine _warp_aff;
        XYZ_SB_FE_Piecewise_linear _warp_piece_lin;

        // In C:
        // static fe_warp_conf_t fe_warp_conf[FE_WARP_ID_MAX + 1] = {
        //     {fe_warp_inverse_linear_set_parameters,
        //     fe_warp_inverse_linear_doc,
        //     fe_warp_inverse_linear_id,
        //     fe_warp_inverse_linear_n_param,
        //     fe_warp_inverse_linear_warped_to_unwarped,
        //     fe_warp_inverse_linear_unwarped_to_warped,
        //     fe_warp_inverse_linear_print},     /* Inverse linear warping */
        //     {fe_warp_affine_set_parameters,
        //     fe_warp_affine_doc,
        //     fe_warp_affine_id,
        //     fe_warp_affine_n_param,
        //     fe_warp_affine_warped_to_unwarped,
        //     fe_warp_affine_unwarped_to_warped,
        //     fe_warp_affine_print},     /* Affine warping */
        //     {fe_warp_piecewise_linear_set_parameters,
        //     fe_warp_piecewise_linear_doc,
        //     fe_warp_piecewise_linear_id,
        //     fe_warp_piecewise_linear_n_param,
        //     fe_warp_piecewise_linear_warped_to_unwarped,
        //     fe_warp_piecewise_linear_unwarped_to_warped,
        //     fe_warp_piecewise_linear_print},   /* Piecewise_Linear warping */
        // };

        // In C++ (madness) (https://blog.mbedded.ninja/programming/languages/c-plus-plus/callbacks/)
        fe_warp_conf_t fe_warp_conf[FE_WARP_ID_MAX + 1] = {
            {std::bind(&XYZ_SB_FE_Warp_inverse_linear::fe_warp_inverse_linear_set_parameters, _warp_inv_lin, std::placeholders::_1, std::placeholders::_2),
            std::bind(&XYZ_SB_FE_Warp_inverse_linear::fe_warp_inverse_linear_doc, _warp_inv_lin),
            std::bind(&XYZ_SB_FE_Warp_inverse_linear::fe_warp_inverse_linear_id, _warp_inv_lin),
            std::bind(&XYZ_SB_FE_Warp_inverse_linear::fe_warp_inverse_linear_n_param, _warp_inv_lin),
            std::bind(&XYZ_SB_FE_Warp_inverse_linear::fe_warp_inverse_linear_warped_to_unwarped, _warp_inv_lin, std::placeholders::_1),
            std::bind(&XYZ_SB_FE_Warp_inverse_linear::fe_warp_inverse_linear_unwarped_to_warped, _warp_inv_lin, std::placeholders::_1),
            std::bind(&XYZ_SB_FE_Warp_inverse_linear::fe_warp_inverse_linear_print, _warp_inv_lin, std::placeholders::_1)},     /* Inverse linear warping */
            {std::bind(&XYZ_SB_FE_Warp_affine::fe_warp_affine_set_parameters, _warp_aff, std::placeholders::_1, std::placeholders::_2),
            std::bind(&XYZ_SB_FE_Warp_affine::fe_warp_affine_doc, _warp_aff),
            std::bind(&XYZ_SB_FE_Warp_affine::fe_warp_affine_id, _warp_aff),
            std::bind(&XYZ_SB_FE_Warp_affine::fe_warp_affine_n_param, _warp_aff),
            std::bind(&XYZ_SB_FE_Warp_affine::fe_warp_affine_warped_to_unwarped, _warp_aff, std::placeholders::_1),
            std::bind(&XYZ_SB_FE_Warp_affine::fe_warp_affine_unwarped_to_warped, _warp_aff, std::placeholders::_1),
            std::bind(&XYZ_SB_FE_Warp_affine::fe_warp_affine_print, _warp_aff, std::placeholders::_1)},    /* Affine warping */
            {std::bind(&XYZ_SB_FE_Piecewise_linear::fe_warp_piecewise_linear_set_parameters, _warp_piece_lin, std::placeholders::_1, std::placeholders::_2),
            std::bind(&XYZ_SB_FE_Piecewise_linear::fe_warp_piecewise_linear_doc, _warp_piece_lin),
            std::bind(&XYZ_SB_FE_Piecewise_linear::fe_warp_piecewise_linear_id, _warp_piece_lin),
            std::bind(&XYZ_SB_FE_Piecewise_linear::fe_warp_piecewise_linear_n_param, _warp_piece_lin),
            std::bind(&XYZ_SB_FE_Piecewise_linear::fe_warp_piecewise_linear_warped_to_unwarped, _warp_piece_lin, std::placeholders::_1),
            std::bind(&XYZ_SB_FE_Piecewise_linear::fe_warp_piecewise_linear_unwarped_to_warped, _warp_piece_lin, std::placeholders::_1),
            std::bind(&XYZ_SB_FE_Piecewise_linear::fe_warp_piecewise_linear_print, _warp_piece_lin, std::placeholders::_1)},   /* Piecewise_Linear warping */
        };


    public:

        int
        fe_warp_set(melfb_t *mel, const char *id_name)
        {
            uint32 i;

            for (i = 0; name2id[i]; i++) {
                if (strcmp(id_name, name2id[i]) == 0) {
                    mel->warp_id = i;
                    break;
                }
            }

            if (name2id[i] == NULL) {
                for (i = 0; __name2id[i]; i++) {
                    if (strcmp(id_name, __name2id[i]) == 0) {
                        mel->warp_id = i;
                        break;
                    }
                }
                if (__name2id[i] == NULL) {
                    E_ERROR("Unimplemented warping function %s\n", id_name);
                    E_ERROR("Implemented functions are:\n");
                    for (i = 0; name2id[i]; i++) {
                        fprintf(stderr, "\t%s\n", name2id[i]);
                    }
                    mel->warp_id = FE_WARP_ID_NONE;

                    return FE_START_ERROR;
                }
            }

            return FE_SUCCESS;
        }

        void
        fe_warp_set_parameters(melfb_t *mel, char const *param_str, float sampling_rate)
        {
            if (mel->warp_id <= FE_WARP_ID_MAX) {
                //fe_warp_conf[mel->warp_id].set_parameters(param_str, sampling_rate);
                fe_warp_conf[mel->warp_id].set_parameters(param_str, sampling_rate);
            }
            else if (mel->warp_id == FE_WARP_ID_NONE) {
                E_FATAL("feat module must be configured w/ a valid ID\n");
            }
            else {
                E_FATAL
                    ("fe_warp module misconfigured with invalid fe_warp_id %u\n",
                    mel->warp_id);
            }
        }

        const char *
        fe_warp_doc(melfb_t *mel)
        {
            if (mel->warp_id <= FE_WARP_ID_MAX) {
                return fe_warp_conf[mel->warp_id].doc();
            }
            else if (mel->warp_id == FE_WARP_ID_NONE) {
                E_FATAL("fe_warp module must be configured w/ a valid ID\n");
            }
            else {
                E_FATAL
                    ("fe_warp module misconfigured with invalid fe_warp_id %u\n",
                    mel->warp_id);
            }

            return NULL;
        }

        uint32
        fe_warp_id(melfb_t *mel)
        {
            if (mel->warp_id <= FE_WARP_ID_MAX) {
                assert(mel->warp_id == fe_warp_conf[mel->warp_id].id());
                return mel->warp_id;
            }
            else if (mel->warp_id != FE_WARP_ID_NONE) {
                E_FATAL
                    ("fe_warp module misconfigured with invalid fe_warp_id %u\n",
                    mel->warp_id);
            }

            return FE_WARP_ID_NONE;
        }

        uint32
        fe_warp_n_param(melfb_t *mel)
        {
            if (mel->warp_id <= FE_WARP_ID_MAX) {
                return fe_warp_conf[mel->warp_id].n_param();
            }
            else if (mel->warp_id == FE_WARP_ID_NONE) {
                E_FATAL("fe_warp module must be configured w/ a valid ID\n");
            }
            else {
                E_FATAL
                    ("fe_warp module misconfigured with invalid fe_warp_id %u\n",
                    mel->warp_id);
            }

            return 0;
        }

        float
        fe_warp_warped_to_unwarped(melfb_t *mel, float nonlinear)
        {
            if (mel->warp_id <= FE_WARP_ID_MAX) {
                return fe_warp_conf[mel->warp_id].warped_to_unwarped(nonlinear);
            }
            else if (mel->warp_id == FE_WARP_ID_NONE) {
                E_FATAL("fe_warp module must be configured w/ a valid ID\n");
            }
            else {
                E_FATAL
                    ("fe_warp module misconfigured with invalid fe_warp_id %u\n",
                    mel->warp_id);
            }

            return 0;
        }

        float
        fe_warp_unwarped_to_warped(melfb_t *mel,float linear)
        {
            if (mel->warp_id <= FE_WARP_ID_MAX) {
                return fe_warp_conf[mel->warp_id].unwarped_to_warped(linear);
            }
            else if (mel->warp_id == FE_WARP_ID_NONE) {
                E_FATAL("fe_warp module must be configured w/ a valid ID\n");
            }
            else {
                E_FATAL
                    ("fe_warp module misconfigured with invalid fe_warp_id %u\n",
                    mel->warp_id);
            }

            return 0;
        }

        void
        fe_warp_print(melfb_t *mel, const char *label)
        {
            if (mel->warp_id <= FE_WARP_ID_MAX) {
                fe_warp_conf[mel->warp_id].print(label);
            }
            else if (mel->warp_id == FE_WARP_ID_NONE) {
                E_FATAL("fe_warp module must be configured w/ a valid ID\n");
            }
            else {
                E_FATAL
                    ("fe_warp module misconfigured with invalid fe_warp_id %u\n",
                    mel->warp_id);
            }
        }

        
};

#define FE_WARP_NO_SIZE	0xffffffff

// #ifdef __cplusplus
// }
// #endif


#endif /* FE_WARP_H */ 