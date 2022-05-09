#include <stdexcept>
#include <limits>
#include <iostream>
//#include <chrono>
#include <math.h> 
/* SphinxBase headers. */
//#include <xyzsphinxbase/pio.h>
// #include <xyzsphinxbase/err.h>
// #include <xyzsphinxbase/strfuncs.h>
// #include <xyzsphinxbase/filename.h>
#include <xyzsphinxbase/byteorder.h>

/* SphinxBase headers. */
#include <xyzsphinxbase/err.h>
#include <xyzsphinxbase/strfuncs.h>
#include <xyzsphinxbase/filename.h>
#include <xyzsphinxbase/pio.h>
#include <xyzsphinxbase/jsgf.h>
#include <xyzsphinxbase/hash_table.h>

/* Local headers. */
#include "cmdln_macro.h"
#include "pocketsphinx.h"
#include "pocketsphinx_internal.h"
#include "ps_lattice_internal.h"
#include "phone_loop_search.h"
#include "kws_search.h"
#include "fsg_search_internal.h"
#include "ngram_search.h"
#include "ngram_search_fwdtree.h"
#include "ngram_search_fwdflat.h"
#include "allphone_search.h"
#include "state_align_search.h"

#define MAX_INT32		((int32) 0x7fffffff)
#define MAX_N_FRAMES MAX_INT32

/**
 * States in utterance processing.
 */
// typedef enum acmod_state_e {
//     ACMOD_IDLE,		/**< Not in an utterance. */
//     ACMOD_STARTED,      /**< Utterance started, no data yet. */
//     ACMOD_PROCESSING,   /**< Utterance in progress. */
//     ACMOD_ENDED         /**< Utterance ended, still buffering. */
// } acmod_state_t;

#define FEAT_DCEP_WIN		2
#define feat_n_stream(f) ((f)->n_stream)
#define feat_cepsize(f)		((f)->cepsize)
#define feat_stream_len(f,i) ((f)->stream_len[i])
#define feat_window_size(f) ((f)->window_size)
#define cep_dump_dbg(fcb,mfc,nfr,text)


static const arg_t _ps_args_def[] = {
    POCKETSPHINX_OPTIONS,
    CMDLN_EMPTY_OPTION
};

/* Feature and front-end parameters that may be in feat.params */
static const arg_t _feat_defn[] = {
    waveform_to_cepstral_command_line_macro(),
    cepstral_to_feature_command_line_macro(),
    CMDLN_EMPTY_OPTION
};

// /* PocketSphinx headers. */
// #include "pocketsphinx.h"

// /* S3kr3t headerz. */
// #include "pocketsphinx_internal.h"

/* Silvio Moioli: setbuf doesn't exist in Windows CE */
#if defined(_WIN32_WCE)
    void setbuf(FILE* file, char* buf){
    }
#endif

static const arg_t batch_args_def[] = {
    POCKETSPHINX_OPTIONS,
    /* Various options specific to batch-mode processing. */
    /* Argument file. */
    { "-argfile",
      ARG_STRING,
      NULL,
      "Argument file giving extra arguments." },
    /* Control file. */
    { "-ctl",
      ARG_STRING,
      NULL,
      "Control file listing utterances to be processed" },
    { "-ctloffset",
      ARG_INT32,
      "0",
      "No. of utterances at the beginning of -ctl file to be skipped" },
    { "-ctlcount",
      ARG_INT32,
      "-1",
      "No. of utterances to be processed (after skipping -ctloffset entries)" },
    { "-ctlincr",
      ARG_INT32,
      "1",
      "Do every Nth line in the control file" },
    { "-mllrctl",
      ARG_STRING,
      NULL,
      "Control file listing MLLR transforms to use for each utterance" },
    { "-mllrdir",
      ARG_STRING,
      NULL,
      "Base directory for MLLR transforms" },
    { "-mllrext",
      ARG_STRING,
      NULL,
      "File extension for MLLR transforms (including leading dot)" },
    { "-lmnamectl",
      ARG_STRING,
      NULL,
      "Control file listing LM name to use for each utterance" },
    { "-fsgctl",
      ARG_STRING,
      NULL,
      "Control file listing FSG file to use for each utterance" },
    { "-fsgdir",
      ARG_STRING,
      NULL,
      "Base directory for FSG files" },
    { "-fsgext",
      ARG_STRING,
      NULL,
      "File extension for FSG files (including leading dot)" },
    { "-alignctl",
      ARG_STRING,
      NULL,
      "Control file listing transcript files to force-align to utts" },
    { "-aligndir",
      ARG_STRING,
      NULL,
      "Base directory for transcript files" },
    { "-alignext",
      ARG_STRING,
      NULL,
      "File extension for transcript files (including leading dot)" },

    /* Input file types and locations. */
    { "-adcin",
      ARG_BOOLEAN,
      "no",
      "Input is raw audio data" },
    { "-adchdr",
      ARG_INT32,
      "0",
      "Size of audio file header in bytes (headers are ignored)" },
    { "-senin",
      ARG_BOOLEAN,
      "no",
      "Input is senone score dump files" },
    { "-cepdir",
      ARG_STRING,
      NULL,
      "Input files directory (prefixed to filespecs in control file)" },
    { "-cepext",
      ARG_STRING,
      ".mfc",
      "Input files extension (suffixed to filespecs in control file)" },

    /* Output files. */
    { "-hyp",
      ARG_STRING,
      NULL,
      "Recognition output file name" },
    { "-hypseg",
      ARG_STRING,
      NULL,
      "Recognition output with segmentation file name" },
    { "-ctm",
      ARG_STRING,
      NULL,
      "Recognition output in CTM file format (may require post-sorting)" },
    { "-outlatdir",
      ARG_STRING,
      NULL,
      "Directory for dumping word lattices" },
    { "-outlatfmt",
      ARG_STRING,
      "s3",
      "Format for dumping word lattices (s3 or htk)" },
    { "-outlatext",
      ARG_STRING,
      ".lat",
      "Filename extension for dumping word lattices" },
    { "-outlatbeam",
      ARG_FLOAT64,
      "1e-5",
      "Minimum posterior probability for output lattice nodes" },
    { "-build_outdirs",
      ARG_BOOLEAN,
      "yes",
      "Create missing subdirectories in output directory" },
    { "-nbestdir",
      ARG_STRING,
      NULL,
      "Directory for writing N-best hypothesis lists" },
    { "-nbestext",
      ARG_STRING,
      ".hyp",
      "Extension for N-best hypothesis list files" },
    { "-nbest",
      ARG_INT32,
      "0",
      "Number of N-best hypotheses to write to -nbestdir (0 for no N-best)" },

    CMDLN_EMPTY_OPTION
};

int
_fe_process_frames(fe_t *fe,
                const int16 **inout_spch,
                size_t *inout_nsamps,
                mfcc_t **buf_cep,
                int32 *inout_nframes,
                int32 *out_frameidx)
{
    return fe_process_frames_ext(fe, inout_spch, inout_nsamps, buf_cep, inout_nframes, NULL, NULL, out_frameidx);
}


class XYZ_Batch {

    private:
        ps_decoder_t *_ps;
        cmd_ln_t *_config;
 
        void* _audio_buffer;
        size_t _audio_buffer_size; 
        int _argc; 
        char **_argv;
     
    public:
        char _result[512];
        int _result_size;
        float* _cmean_t0;
        float* _cmean_t1;
        float* _cmean_tn;




        void init(void* audio_buffer, size_t audio_buffer_size, int argc, char **argv) {

            _audio_buffer = audio_buffer;
            _audio_buffer_size=audio_buffer_size;
            _argc=argc;

            _argv = (char**)malloc(argc * sizeof(char*));
            for(int i =0; i< argc; i++){
                if(argv[i]!=NULL) {
                    _argv[i]=(char*)malloc(sizeof(char)*strlen(argv[i])+1);
                    strcpy(_argv[i],argv[i]);
                } else {
                    printf("Error: copying parameters.");
                }
            }

            _cmean_t0 = (float*)malloc(sizeof(float)*13);
            _cmean_t1 = (float*)malloc(sizeof(float)*13);
            _cmean_tn = (float*)malloc(sizeof(float)*13);

        }

        int init_recognition() {
            

            char const *ctl;

            _config = cmd_ln_parse_r(NULL, batch_args_def, _argc, _argv, TRUE);
            
            

            /* Handle argument file as -argfile. */
            if (_config && (ctl = cmd_ln_str_r(_config, "-argfile")) != NULL) {
                _config = cmd_ln_parse_file_r(_config, batch_args_def, ctl, FALSE);
            }
            
            if (_config == NULL) {
                /* This probably just means that we got no arguments. */
                return 1;
            }

            if ((ctl = cmd_ln_str_r(_config, "-ctl")) == NULL) {
                E_FATAL("-ctl argument not present, nothing to do in batch mode!\n");
            }
            // if ((ctlfh = fopen(ctl, "r")) == NULL) {
            //     E_FATAL_SYSTEM("Failed to open control file '%s'", ctl);
            // }
            
            ps_default_search_args(_config);
            
            ps_plus_init();
            
            if (_ps == NULL) {
                    cmd_ln_free_r(_config);
                    return 1;
                }

            //E_INFO("%s COMPILED ON: %s, AT: %s\n\n", _argv[0], __DATE__, __TIME__);
            if(_config==NULL) printf("config is NULL !!!!\n");
            if(_ps==NULL) printf("ps is NULL!!!\n");
            debug_extract_cepstral_mean(_cmean_t0);

           

            return 0;
        }

        static void
        //void
        _feat_1s_c_d_dd_cep2feat(feat_t * fcb, mfcc_t ** mfc, mfcc_t ** feat)
        {
            mfcc_t *f;
            mfcc_t *w, *_w;
            mfcc_t *w1, *w_1, *_w1, *_w_1;
            mfcc_t d1, d2;
            int32 i;

            assert(fcb);
            assert(feat_n_stream(fcb) == 1);
            assert(feat_stream_len(fcb, 0) == (uint32)feat_cepsize(fcb) * 3);
            assert(feat_window_size(fcb) == FEAT_DCEP_WIN + 1);

            /* CEP */
            //mfcc coefficients (13):
            memcpy(feat[0], mfc[0], feat_cepsize(fcb) * sizeof(mfcc_t));

            /*
            * DCEP: mfc[w] - mfc[-w], where w = FEAT_DCEP_WIN;
            */
            f = feat[0] + feat_cepsize(fcb);
            w = mfc[FEAT_DCEP_WIN];
            _w = mfc[-FEAT_DCEP_WIN];
            
            //deltas:
            for (i = 0; i < feat_cepsize(fcb); i++)
                f[i] = w[i] - _w[i];

            /* 
            * D2CEP: (mfc[w+1] - mfc[-w+1]) - (mfc[w-1] - mfc[-w-1]), 
            * where w = FEAT_DCEP_WIN 
            */
            f += feat_cepsize(fcb);

            w1 = mfc[FEAT_DCEP_WIN + 1];
            _w1 = mfc[-FEAT_DCEP_WIN + 1];
            w_1 = mfc[FEAT_DCEP_WIN - 1];
            _w_1 = mfc[-FEAT_DCEP_WIN - 1];
            //delta deltas:
            for (i = 0; i < feat_cepsize(fcb); i++) {
                d1 = w1[i] - _w1[i];
                d2 = w_1[i] - _w_1[i];

                f[i] = d1 - d2;
            }
        }



        void process(){
             process_one_ctl_line(0, -1);
        }

        void terminate(){
            ps_free(_ps);
            cmd_ln_free_r(_config);
            
            
            for(int i =0; i< _argc; i++){
                free(_argv[i]);
            }

            free(_argv);

            free(_cmean_t0);
            free(_cmean_t1);
        }

        int
        process_one_ctl_line(int32 sf, int32 ef)
        {
            FILE *infh;
            //char const *cepdir, *cepext;
            //char *infile;

            if (ef != -1 && ef < sf) {
                E_ERROR("End frame %d is < start frame %d\n", ef, sf);
                return -1;
            }
            
            // cepdir = cmd_ln_str_r(config, "-cepdir");
            // cepext = cmd_ln_str_r(config, "-cepext");

            /* Build input filename. */
            // infile = string_join(cepdir ? cepdir : "",
            //                     "/", file,
            //                     cepext ? cepext : "", NULL);
            // if (uttid == NULL) uttid = file;
            debug_extract_cepstral_mean(_cmean_t1);
            infh = fmemopen(_audio_buffer, _audio_buffer_size ,"rb");
            // if ((infh = fopen(infile, "rb")) == NULL) {
            //     E_ERROR_SYSTEM("Failed to open %s", infile);
            //     ckd_free(infile);
            //     return -1;
            // }
            /* Build output directories. */
            // if (cmd_ln_boolean_r(config, "-build_outdirs"))
            //     build_outdirs(config, uttid);

            // if (cmd_ln_boolean_r(config, "-senin")) {
            //     /* start and end frames not supported. */
            //     ps_decode_senscr(ps, infh);
            // }
            // else 
            if (cmd_ln_boolean_r(_config, "-adcin")) {
                
                if (ef != -1) {
                    ef = (int32)((ef - sf)
                                * (cmd_ln_float32_r(_config, "-samprate")
                                    / cmd_ln_int32_r(_config, "-frate"))
                                + (cmd_ln_float32_r(_config, "-samprate")
                                    * cmd_ln_float32_r(_config, "-wlen")));
                }
                sf = (int32)(sf
                            * (cmd_ln_float32_r(_config, "-samprate")
                                / cmd_ln_int32_r(_config, "-frate")));
                fseek(infh, cmd_ln_int32_r(_config, "-adchdr") + sf * sizeof(int16), SEEK_SET);
                decode_raw(infh, ef);
            }
            // else {
            //     mfcc_t **mfcs;
            //     int nfr;

            //     if (NULL == (mfcs = read_mfc_file(infh, sf, ef, &nfr,
            //                                     cmd_ln_int32_r(config, "-ceplen")))) {
            //         E_ERROR("Failed to read MFCC from the file '%s'\n", file);//infile
            //         fclose(infh);
            //         //ckd_free(infile);
            //         return -1;
            //     }
            //     ps_start_stream(ps);
            //     ps_start_utt(ps);
            //     ps_process_cep(ps, mfcs, nfr, FALSE, TRUE);
            //     ps_end_utt(ps);
            //     ckd_free_2d(mfcs);
            // }
            fclose(infh);
            //ckd_free(infile);
            return 0;
        }

        long
        decode_raw(FILE *rawfh,
                    long maxsamps)
        {
            int16 *data;
            size_t total, pos, endpos;

            ps_start_stream(_ps);

            //Batch scan change:
            //ps_start_utt(_ps); //--> this requires the whole decoder loaded (language model, dictionary and so on...which we are not using at all)
            _ps->acmod->state = ACMOD_STARTED;
            //acmod_start_utt(_ps->acmod); // we dont use it either
            
            /* If this file is seekable or maxsamps is specified, then decode
            * the whole thing at once. */
            if (maxsamps != -1) {
                // data = (int16*)ckd_calloc(maxsamps, sizeof(*data));
                // total = fread(data, sizeof(*data), maxsamps, rawfh);
                // ps_process_raw(_ps, data, total, FALSE, TRUE);
                // ckd_free(data);
                
            } else if ((pos = ftell(rawfh)) >= 0) {
                fseek(rawfh, 0, SEEK_END);
                endpos = ftell(rawfh);
                fseek(rawfh, pos, SEEK_SET);
                maxsamps = endpos - pos;

                data = (int16*)ckd_calloc(maxsamps, sizeof(*data));
                total = fread(data, sizeof(*data), maxsamps, rawfh);
                //ps_process_raw(_ps, data, total, FALSE, TRUE);
                _ps_process_raw(_ps, data, total, FALSE, TRUE);
                //_acmod_process_full_raw(_ps->acmod, &data, &total);
                ckd_free(data);
                debug_extract_cepstral_mean(_cmean_t1);
            } else {
                /* Otherwise decode it in a stream. */
                total = 0;
                while (!feof(rawfh)) {
                    int16 data[256];
                    size_t nread;

                    nread = fread(data, sizeof(*data), sizeof(data)/sizeof(*data), rawfh);
                    ps_process_raw(_ps, data, nread, FALSE, FALSE);
                    total += nread;
                }
                
            }
            ////Batch scan change:
            //ps_end_utt(_ps);//--> this requires the whole decoder loaded (language model, dictionary and so on...which we are not using at all)
            acmod_end_utt(_ps->acmod);//we replace it with this, or we could just ignore it or just set: _ps->acmod->state = ACMOD_ENDED
             try {
                debug_extract_cepstral_mean(_cmean_tn);
                extract_cepstral_mean();
             }catch( std::exception& e){
                std::cerr << e.what() << std::endl;
            }
            return total;
        }
//#define ckd_calloc_2d(d1,d2,sz)	__ckd_calloc_2d__((d1),(d2),(sz),__FILE__,__LINE__)
        int
        _ps_process_raw(ps_decoder_t *ps,
                    int16 const *data,
                    size_t n_samples,
                    int no_search,
                    int full_utt)
        {
            int n_searchfr = 0;

            if (ps->acmod->state == ACMOD_IDLE) {
            E_ERROR("Failed to process data, utterance is not started. Use start_utt to start it\n");
            return 0;
            }

            if (no_search)
                acmod_set_grow(ps->acmod, TRUE);

            while (n_samples) {
                int nfr;

                /* Process some data into features. */
                if ((nfr = _acmod_process_raw(ps->acmod, &data,
                                            &n_samples, full_utt)) < 0)
                    return nfr;

                /* Score and search as much data as possible */
                if (no_search)
                    continue;
                // if ((nfr = ps_search_forward(ps)) < 0)
                //     return nfr;
                n_searchfr += nfr;
            }

            return n_searchfr;
        }

        int
        _acmod_process_raw(acmod_t *acmod,
                        int16 const **inout_raw,
                        size_t *inout_n_samps,
                        int full_utt)
        {
            int32 ncep;
            int32 out_frameidx;
            int16 const *prev_audio_inptr;
            
            /* If this is a full utterance, process it all at once. */
            if (full_utt)
                return _acmod_process_full_raw(acmod, inout_raw, inout_n_samps);

            /* Append MFCCs to the end of any that are previously in there
            * (in practice, there will probably be none) */
            if (inout_n_samps && *inout_n_samps) {
                int inptr;
                int32 processed_samples;

                prev_audio_inptr = *inout_raw;
                /* Total number of frames available. */
                ncep = acmod->n_mfc_alloc - acmod->n_mfc_frame;
                /* Where to start writing them (circular buffer) */
                inptr = (acmod->mfc_outidx + acmod->n_mfc_frame) % acmod->n_mfc_alloc;

                /* Write them in two (or more) parts if there is wraparound. */
                while (inptr + ncep > acmod->n_mfc_alloc) {
                    int32 ncep1 = acmod->n_mfc_alloc - inptr;
                    if (fe_process_frames(acmod->fe, inout_raw, inout_n_samps,
                                        acmod->mfc_buf + inptr, &ncep1, &out_frameidx) < 0)
                        return -1;
                
                if (out_frameidx > 0)
                acmod->utt_start_frame = out_frameidx;

                    processed_samples = *inout_raw - prev_audio_inptr;
                if (processed_samples + acmod->rawdata_pos < acmod->rawdata_size) {
                memcpy(acmod->rawdata + acmod->rawdata_pos, prev_audio_inptr, processed_samples * sizeof(int16));
                acmod->rawdata_pos += processed_samples;
                }
                    /* Write to logging file if any. */
                    if (acmod->rawfh) {
                        fwrite(prev_audio_inptr, sizeof(int16),
                            processed_samples,
                            acmod->rawfh);
                    }
                    prev_audio_inptr = *inout_raw;
                    
                    /* ncep1 now contains the number of frames actually
                    * processed.  This is a good thing, but it means we
                    * actually still might have some room left at the end of
                    * the buffer, hence the while loop.  Unfortunately it
                    * also means that in the case where we are really
                    * actually done, we need to get out totally, hence the
                    * goto. */
                    acmod->n_mfc_frame += ncep1;
                    ncep -= ncep1;
                    inptr += ncep1;
                    inptr %= acmod->n_mfc_alloc;
                    if (ncep1 == 0)
                    goto alldone;
                }

                assert(inptr + ncep <= acmod->n_mfc_alloc);        
                if (fe_process_frames(acmod->fe, inout_raw, inout_n_samps,
                                    acmod->mfc_buf + inptr, &ncep, &out_frameidx) < 0)
                    return -1;

            if (out_frameidx > 0)
                acmod->utt_start_frame = out_frameidx;

            
            processed_samples = *inout_raw - prev_audio_inptr;
            if (processed_samples + acmod->rawdata_pos < acmod->rawdata_size) {
                memcpy(acmod->rawdata + acmod->rawdata_pos, prev_audio_inptr, processed_samples * sizeof(int16));
                acmod->rawdata_pos += processed_samples;
            }
                if (acmod->rawfh) {
                    fwrite(prev_audio_inptr, sizeof(int16),
                        processed_samples, acmod->rawfh);
                }
                prev_audio_inptr = *inout_raw;
                acmod->n_mfc_frame += ncep;
            alldone:
                ;
            }

            /* Hand things off to acmod_process_cep. */
            return _acmod_process_mfcbuf(acmod);
        }

        /**
         * Process MFCCs that are in the internal buffer into features.
         */
        static int32
        _acmod_process_mfcbuf(acmod_t *acmod)
        {
            mfcc_t **mfcptr;
            int32 ncep;

            ncep = acmod->n_mfc_frame;
            /* Also do this in two parts because of the circular mfc_buf. */
            if (acmod->mfc_outidx + ncep > acmod->n_mfc_alloc) {
                int32 ncep1 = acmod->n_mfc_alloc - acmod->mfc_outidx;
                int saved_state = acmod->state;

                /* Make sure we don't end the utterance here. */
                if (acmod->state == ACMOD_ENDED)
                    acmod->state = ACMOD_PROCESSING;
                mfcptr = acmod->mfc_buf + acmod->mfc_outidx;
                ncep1 = acmod_process_cep(acmod, &mfcptr, &ncep1, FALSE);
                /* It's possible that not all available frames were filled. */
                ncep -= ncep1;
                acmod->n_mfc_frame -= ncep1;
                acmod->mfc_outidx += ncep1;
                acmod->mfc_outidx %= acmod->n_mfc_alloc;
                /* Restore original state (could this really be the end) */
                acmod->state = saved_state;
            }
            mfcptr = acmod->mfc_buf + acmod->mfc_outidx;
            ncep = acmod_process_cep(acmod, &mfcptr, &ncep, FALSE);
            acmod->n_mfc_frame -= ncep;
            acmod->mfc_outidx += ncep;
            acmod->mfc_outidx %= acmod->n_mfc_alloc;
            return ncep;
        }

        static int
        //int
        _acmod_process_full_raw(acmod_t *acmod,
                            const int16 **inout_raw,
                            size_t *inout_n_samps)
        {
            int32 nfr, ntail;
            mfcc_t **cepptr;

            /* Write to logging file if any. */
            if (*inout_n_samps + acmod->rawdata_pos < (long unsigned int)(acmod->rawdata_size)) {
            memcpy(acmod->rawdata + acmod->rawdata_pos, *inout_raw, *inout_n_samps * sizeof(int16));
            acmod->rawdata_pos += *inout_n_samps;
            }
            if (acmod->rawfh)
                fwrite(*inout_raw, sizeof(int16), *inout_n_samps, acmod->rawfh);
            /* Resize mfc_buf to fit. */
            if (fe_process_frames(acmod->fe, NULL, inout_n_samps, NULL, &nfr, NULL) < 0)
                return -1;
            if (acmod->n_mfc_alloc < nfr + 1) {
                ckd_free_2d(acmod->mfc_buf);
                acmod->mfc_buf = (mfcc_t**)ckd_calloc_2d(nfr + 1, fe_get_output_size(acmod->fe),
                                            sizeof(**acmod->mfc_buf));
                // acmod->mfc_buf = (mfcc_t**)___ckd_calloc_2d__(nfr + 1, fe_get_output_size(acmod->fe),
                //                             sizeof(**acmod->mfc_buf));
                acmod->n_mfc_alloc = nfr + 1;
            }
            acmod->n_mfc_frame = 0;
            acmod->mfc_outidx = 0;
            fe_start_utt(acmod->fe);
            if (fe_process_frames(acmod->fe, inout_raw, inout_n_samps, acmod->mfc_buf, &nfr, NULL) < 0) //<<--- This calculates mfcc's
                return -1;
            fe_end_utt(acmod->fe, acmod->mfc_buf[nfr], &ntail); //<<--- Recalculates mfcc for the last frame
            nfr += ntail;

            cepptr = acmod->mfc_buf;
            nfr = _acmod_process_full_cep(acmod, &cepptr, &nfr);
            acmod->n_mfc_frame = 0;
            return nfr;
        }

        

        static int
        //int
        _acmod_process_full_cep(acmod_t *acmod,
                            mfcc_t ***inout_cep,
                            int *inout_n_frames)
        {
            int32 nfr;

            /* Write to file. */
            if (acmod->mfcfh)
                printf("writing to a file.");
                //acmod_log_mfc(acmod, *inout_cep, *inout_n_frames);

            /* Resize feat_buf to fit. */
            if (acmod->n_feat_alloc < *inout_n_frames) {

                if (*inout_n_frames > MAX_N_FRAMES)
                    E_FATAL("Batch processing can not process more than %d frames "
                            "at once, requested %d\n", MAX_N_FRAMES, *inout_n_frames);

                feat_array_free(acmod->feat_buf);
                acmod->feat_buf = feat_array_alloc(acmod->fcb, *inout_n_frames);
                acmod->n_feat_alloc = *inout_n_frames;
                acmod->n_feat_frame = 0;
                acmod->feat_outidx = 0;
            }
            /* Make dynamic features. */
            nfr = _feat_s2mfc2feat_live(acmod->fcb, *inout_cep, inout_n_frames,
                                    TRUE, TRUE, acmod->feat_buf);
            acmod->n_feat_frame = nfr;
            assert(acmod->n_feat_frame <= acmod->n_feat_alloc);
            *inout_cep += *inout_n_frames;
            *inout_n_frames = 0;

            return nfr;
        }

        //int32
        static int32
        _feat_s2mfc2feat_live(feat_t * fcb, mfcc_t ** uttcep, int32 *inout_ncep,
                    int32 beginutt, int32 endutt, mfcc_t *** ofeat)
        {
            int32 win, cepsize, nbufcep;
            int32 i, j, nfeatvec;
            int32 zero = 0;

            /* Avoid having to check this everywhere. */
            if (inout_ncep == NULL) inout_ncep = &zero;

            /* Special case for entire utterances. */
            if (beginutt && endutt && *inout_ncep > 0)
                return _feat_s2mfc2feat_block_utt(fcb, uttcep, *inout_ncep, ofeat);

            win = feat_window_size(fcb);
            cepsize = feat_cepsize(fcb);

            /* Empty the input buffer on start of utterance. */
            if (beginutt)
                fcb->bufpos = fcb->curpos;

            /* Calculate how much data is in the buffer already. */
            nbufcep = fcb->bufpos - fcb->curpos;
            if (nbufcep < 0)
            nbufcep = fcb->bufpos + LIVEBUFBLOCKSIZE - fcb->curpos;
            /* Add any data that we have to replicate. */
            if (beginutt && *inout_ncep > 0)
                nbufcep += win;
            if (endutt)
                nbufcep += win;

            /* Only consume as much input as will fit in the buffer. */
            if (nbufcep + *inout_ncep > LIVEBUFBLOCKSIZE) {
                /* We also can't overwrite the trailing window, hence the
                * reason why win is subtracted here. */
                *inout_ncep = LIVEBUFBLOCKSIZE - nbufcep - win;
                /* Cancel end of utterance processing. */
                endutt = FALSE;
            }

            /* FIXME: Don't modify the input! */
            _feat_cmn(fcb, uttcep, *inout_ncep, beginutt, endutt);
            _feat_agc(fcb, uttcep, *inout_ncep, beginutt, endutt);

            /* Replicate first frame into the first win frames if we're at the
            * beginning of the utterance and there was some actual input to
            * deal with.  (FIXME: Not entirely sure why that condition) */
            if (beginutt && *inout_ncep > 0) {
                for (i = 0; i < win; i++) {
                    memcpy(fcb->cepbuf[fcb->bufpos++], uttcep[0],
                        cepsize * sizeof(mfcc_t));
                    fcb->bufpos %= LIVEBUFBLOCKSIZE;
                }
                /* Move the current pointer past this data. */
                fcb->curpos = fcb->bufpos;
                nbufcep -= win;
            }

            /* Copy in frame data to the circular buffer. */
            for (i = 0; i < *inout_ncep; ++i) {
                memcpy(fcb->cepbuf[fcb->bufpos++], uttcep[i],
                    cepsize * sizeof(mfcc_t));
                fcb->bufpos %= LIVEBUFBLOCKSIZE;
            ++nbufcep;
            }

            /* Replicate last frame into the last win frames if we're at the
            * end of the utterance (even if there was no input, so we can
            * flush the output). */
            if (endutt) {
                int32 tpos; /* Index of last input frame. */
                if (fcb->bufpos == 0)
                    tpos = LIVEBUFBLOCKSIZE - 1;
                else
                    tpos = fcb->bufpos - 1;
                for (i = 0; i < win; ++i) {
                    memcpy(fcb->cepbuf[fcb->bufpos++], fcb->cepbuf[tpos],
                        cepsize * sizeof(mfcc_t));
                    fcb->bufpos %= LIVEBUFBLOCKSIZE;
                }
            }

            /* We have to leave the trailing window of frames. */
            nfeatvec = nbufcep - win;
            if (nfeatvec <= 0)
                return 0; /* Do nothing. */

            for (i = 0; i < nfeatvec; ++i) {
                /* Handle wraparound cases. */
                if (fcb->curpos - win < 0 || fcb->curpos + win >= LIVEBUFBLOCKSIZE) {
                    /* Use tmpcepbuf for this case.  Actually, we just need the pointers. */
                    for (j = -win; j <= win; ++j) {
                        int32 tmppos =
                            (fcb->curpos + j + LIVEBUFBLOCKSIZE) % LIVEBUFBLOCKSIZE;
                fcb->tmpcepbuf[win + j] = fcb->cepbuf[tmppos];
                    }
                    fcb->compute_feat(fcb, fcb->tmpcepbuf + win, ofeat[i]);
                }
                else {
                    fcb->compute_feat(fcb, fcb->cepbuf + fcb->curpos, ofeat[i]);
                }
            /* Move the read pointer forward. */
                ++fcb->curpos;
                fcb->curpos %= LIVEBUFBLOCKSIZE;
            }

            // if (fcb->lda)
            //     feat_lda_transform(fcb, ofeat, nfeatvec);

            // if (fcb->subvecs)
            //     feat_subvec_project(fcb, ofeat, nfeatvec);

            return nfeatvec;
        }

        static int32
        //int32
        _feat_s2mfc2feat_block_utt(feat_t * fcb, mfcc_t ** uttcep,
                    int32 nfr, mfcc_t *** ofeat)
        {
            mfcc_t **cepbuf;
            int32 i, win, cepsize;

            win = feat_window_size(fcb);
            cepsize = feat_cepsize(fcb);

            /* Copy and pad out the utterance (this requires that the
            * feature computation functions always access the buffer via
            * the frame pointers, which they do)  */
            cepbuf = (mfcc_t **)ckd_calloc(nfr + win * 2, sizeof(mfcc_t *));
            //cepbuf = (mfcc_t **)___ckd_calloc__(nfr + win * 2, sizeof(mfcc_t *));
            memcpy(cepbuf + win, uttcep, nfr * sizeof(mfcc_t *));

            /* Do normalization before we interpolate on the boundary */    
            _feat_cmn(fcb, cepbuf + win, nfr, 1, 1);//<<--- cmninit
            _feat_agc(fcb, cepbuf + win, nfr, 1, 1);

            /* Now interpolate */    
            for (i = 0; i < win; ++i) {
                cepbuf[i] = fcb->cepbuf[i];
                memcpy(cepbuf[i], uttcep[0], cepsize * sizeof(mfcc_t));
                cepbuf[nfr + win + i] = fcb->cepbuf[win + i];
                memcpy(cepbuf[nfr + win + i], uttcep[nfr - 1], cepsize * sizeof(mfcc_t));
            }
            /* Compute as usual. */
            _feat_compute_utt(fcb, cepbuf, nfr + win * 2, win, ofeat);//<<-- computes deltas and delta-deltas only
            ckd_free(cepbuf);
            return nfr;
        }

        static void
        _feat_cmn(feat_t *fcb, mfcc_t **mfc, int32 nfr, int32 beginutt, int32 endutt)
        {
            cmn_type_t cmn_type = fcb->cmn;

            if (!(beginutt && endutt)
                && cmn_type != CMN_NONE) /* Only cmn_prior in block computation mode. */
                fcb->cmn = cmn_type = CMN_LIVE;

            switch (cmn_type) {
            case CMN_BATCH:
                _cmn(fcb->cmn_struct, mfc, fcb->varnorm, nfr);//<<--- cmninit: calculates cmn_mean (13) and substracts them in mfc for each frame.
                break;
            case CMN_LIVE:
                cmn_live(fcb->cmn_struct, mfc, fcb->varnorm, nfr);
                if (endutt)
                    cmn_live_update(fcb->cmn_struct);
                break;
            default:
                ;
            }
            cep_dump_dbg(fcb, mfc, nfr, "After CMN");
        }

        //void
        static void
        _cmn(cmn_t *cmn, mfcc_t ** mfc, int32 varnorm, int32 n_frame)
        {
            mfcc_t *mfcp;
            mfcc_t t;
            int32 i, f;
            int32 n_pos_frame;

            assert(mfc != NULL);

            if (n_frame <= 0)
                return;

            /* If cmn->cmn_mean wasn't NULL, we need to zero the contents */
            memset(cmn->cmn_mean, 0, cmn->veclen * sizeof(mfcc_t));

            /* Find mean cep vector for this utterance */
            for (f = 0, n_pos_frame = 0; f < n_frame; f++) {
                mfcp = mfc[f];

                /* Skip zero energy frames */
                if (mfcp[0] < 0)
                    continue;

                for (i = 0; i < cmn->veclen; i++) {
                    cmn->cmn_mean[i] += mfcp[i];
                }

                n_pos_frame++;
            }

            for (i = 0; i < cmn->veclen; i++)
                cmn->cmn_mean[i] /= n_pos_frame;

            E_INFO("CMN: ");
            for (i = 0; i < cmn->veclen; i++)
                E_INFOCONT("%5.2f ", MFCC2FLOAT(cmn->cmn_mean[i]));
            E_INFOCONT("\n");
            if (!varnorm) {
                /* Subtract mean from each cep vector */
                for (f = 0; f < n_frame; f++) {
                    mfcp = mfc[f];
                    for (i = 0; i < cmn->veclen; i++)
                        mfcp[i] -= cmn->cmn_mean[i];
                }
            }
            else {
                /* Scale cep vectors to have unit variance along each dimension, and subtract means */
                /* If cmn->cmn_var wasn't NULL, we need to zero the contents */
                memset(cmn->cmn_var, 0, cmn->veclen * sizeof(mfcc_t));

                for (f = 0; f < n_frame; f++) {
                    mfcp = mfc[f];

                    for (i = 0; i < cmn->veclen; i++) {
                        t = mfcp[i] - cmn->cmn_mean[i];
                        cmn->cmn_var[i] += MFCCMUL(t, t);
                    }
                }
                for (i = 0; i < cmn->veclen; i++)
                    /* Inverse Std. Dev, RAH added type case from sqrt */
                    cmn->cmn_var[i] = FLOAT2MFCC(sqrt((float64)n_frame / MFCC2FLOAT(cmn->cmn_var[i])));

                for (f = 0; f < n_frame; f++) {
                    mfcp = mfc[f];
                    for (i = 0; i < cmn->veclen; i++)
                        mfcp[i] = MFCCMUL((mfcp[i] - cmn->cmn_mean[i]), cmn->cmn_var[i]);
                }
            }
        }

        static void
        _feat_agc(feat_t *fcb, mfcc_t **mfc, int32 nfr, int32 beginutt, int32 endutt)
        {
            agc_type_t agc_type = fcb->agc;

            if (!(beginutt && endutt)
                && agc_type != AGC_NONE) /* Only agc_emax in block computation mode. */
                agc_type = AGC_EMAX;

            switch (agc_type) {
            case AGC_MAX:
                agc_max(fcb->agc_struct, mfc, nfr);
                break;
            case AGC_EMAX:
                agc_emax(fcb->agc_struct, mfc, nfr);
                if (endutt)
                    agc_emax_update(fcb->agc_struct);
                break;
            case AGC_NOISE:
                agc_noise(fcb->agc_struct, mfc, nfr);
                break;
            default:
                ;
            }
            cep_dump_dbg(fcb, mfc, nfr, "After AGC");
        }

        static void
        _feat_compute_utt(feat_t *fcb, mfcc_t **mfc, int32 nfr, int32 win, mfcc_t ***feat)
        {
            int32 i;

            cep_dump_dbg(fcb, mfc, nfr, "Incoming features (after padding)");

            /* Create feature vectors */
            for (i = win; i < nfr - win; i++) {
                fcb->compute_feat(fcb, mfc + i, feat[i - win]);
            }

            //feat_print_dbg(fcb, feat, nfr - win * 2, "After dynamic feature computation");

            // if (fcb->lda) {
            //     feat_lda_transform(fcb, feat, nfr - win * 2);
            //     feat_print_dbg(fcb, feat, nfr - win * 2, "After LDA");
            // }

            // if (fcb->subvecs) {
            //     feat_subvec_project(fcb, feat, nfr - win * 2);
            //     feat_print_dbg(fcb, feat, nfr - win * 2, "After subvector projection");
            // }
        }

        // void *
        // ___ckd_calloc_2d__(size_t d1, size_t d2, size_t elemsize)
        // {
        //     char **ref, *mem;
        //     size_t i, offset;

        //     mem =
        //         (char *) ___ckd_calloc__(d1 * d2, elemsize);
        //     ref =
        //         (char **) __ckd_malloc__(d1 * sizeof(void *), "calloc_2d",
        //                                 0);

        //     for (i = 0, offset = 0; i < d1; i++, offset += d2 * elemsize)
        //         ref[i] = mem + offset;

        //     return ref;
        // }

        // void *
        // ___ckd_calloc__(size_t n_elem, size_t elem_size)
        // {
        //     void *mem;

        // #if defined(__ADSPBLACKFIN__) && !defined(__linux__)
        //     if ((mem = heap_calloc(heap_lookup(1),n_elem, elem_size)) == NULL)
        //         if ((mem = heap_calloc(heap_lookup(0),n_elem, elem_size)) == NULL)
        //         {
        //             ckd_fail("calloc(%d,%d) failed from %s(%d), free space: %d\n", n_elem,
        //                 elem_size, caller_file, caller_line,space_unused());
        //         }
        // #else
        //     if ((mem = calloc(n_elem, elem_size)) == NULL) {
        //         // ckd_fail("calloc(%d,%d) failed from %s(%d)\n", n_elem,
        //         //         elem_size, "---", 0);
        //         printf("Error allocating memory.");
        //     }
        // #endif


        //     return mem;
        // }

        void extract_cepstral_mean() {
            
                cmn_t *cmn = _ps->acmod->fcb->cmn_struct;
                char strnum[12];
                _result[0]='\0';
                float32 num;

                int len=0;
                for (int i = 0; i < cmn->veclen; i++) {
                //--------- Method 1:
                    //sprintf(strnum, "%5.2f ", MFCC2FLOAT(cmn->cmn_mean[i]));
                    //gcvt(MFCC2FLOAT(cmn->cmn_mean[i]), 4, strnum);
                    //strcat(_result,strnum);
                    //strcat(_result,",");
                //------- Method 2:
                    num=MFCC2FLOAT(cmn->cmn_mean[i]);
                    //num = cmn->cmn_mean[i];
                    len = snprintf(NULL, 0, "%.2f", num);
                    char *result = (char*)malloc(len + 1);
                    snprintf(result, len + 1, "%.2f", num);
                    result[len]='\0';
                    // do stuff with result
                    strcat(_result,result);
                    strcat(_result,",");
                    free(result);

                }
                strcat(_result, "*");
                _result_size = strlen(_result);
        }

                void debug_extract_cepstral_mean(float *cmean) {
            
                cmn_t *cmn = _ps->acmod->fcb->cmn_struct;

                int len=0;
                for (int i = 0; i < cmn->veclen; i++) {
                    cmean[i]=MFCC2FLOAT(cmn->cmn_mean[i]);
                }
        }

        // int
        // _acmod_process_full_raw(acmod_t *acmod,
        //                     const int16 **inout_raw,
        //                     size_t *inout_n_samps)
        // {
        //     int32 nfr, ntail;
        //     mfcc_t **cepptr;

        //     /* Write to logging file if any. */
        //     if (*inout_n_samps + acmod->rawdata_pos < (long unsigned int)(acmod->rawdata_size)) {
        //     memcpy(acmod->rawdata + acmod->rawdata_pos, *inout_raw, *inout_n_samps * sizeof(int16));
        //     acmod->rawdata_pos += *inout_n_samps;
        //     }
        //     if (acmod->rawfh)
        //         fwrite(*inout_raw, sizeof(int16), *inout_n_samps, acmod->rawfh);
        //     /* Resize mfc_buf to fit. */
        //     if (fe_process_frames(acmod->fe, NULL, inout_n_samps, NULL, &nfr, NULL) < 0)
        //         return -1;
        //     if (acmod->n_mfc_alloc < nfr + 1) {
        //         ckd_free_2d(acmod->mfc_buf);
        //         // acmod->mfc_buf = ckd_calloc_2d(nfr + 1, fe_get_output_size(acmod->fe),
        //         //                             sizeof(**acmod->mfc_buf));
        //         acmod->mfc_buf =(mfcc_t**)___ckd_calloc_2d__(nfr + 1, fe_get_output_size(acmod->fe),
        //                                     sizeof(**acmod->mfc_buf));
        //         acmod->n_mfc_alloc = nfr + 1;
        //     }
        //     acmod->n_mfc_frame = 0;
        //     acmod->mfc_outidx = 0;
        //     fe_start_utt(acmod->fe);
        //     if (fe_process_frames(acmod->fe, inout_raw, inout_n_samps,
        //                         acmod->mfc_buf, &nfr, NULL) < 0)
        //         return -1;
        //     fe_end_utt(acmod->fe, acmod->mfc_buf[nfr], &ntail);
        //     nfr += ntail;

        //     cepptr = acmod->mfc_buf;
        //     nfr = _acmod_process_full_cep(acmod, &cepptr, &nfr);
        //     acmod->n_mfc_frame = 0;
        //     return nfr;
        // }

         void ps_plus_init(void) //d(cmd_ln_t *config, void *buffer, size_t size) // buffer for jsgf grammar
        {
            //ps_decoder_t *ps;
            
            if (!_config) {
            E_ERROR("No configuration specified");
            //return NULL;
            }

            _ps = (ps_decoder_t*)ckd_calloc(1, sizeof(*_ps));
            _ps->refcount = 1;
            if (ps_plus_reinit( ) < 0) {//ered(ps, config, buffer, size) < 0) {
                ps_free(_ps);
                //return NULL;
            }
            //return ps;
            //To remove:
            _ps->acmod->fcb->compute_feat = _feat_1s_c_d_dd_cep2feat;
        }

        int
        ps_plus_reinit(void) //(ps_decoder_t *ps, cmd_ln_t *config, void *buffer, size_t size) // buffer for jsgf grammar
        {

            // using namespace std::chrono;
            // high_resolution_clock::time_point start;
            // high_resolution_clock::time_point end;
	        
            
            
            


            const char *path;
            const char *keyphrase;
            int32 lw;

            if (_config && _config != _ps->config) {
                cmd_ln_free_r(_ps->config);
                _ps->config = cmd_ln_retain(_config);
            }
            

            /* Set up logging. We need to do this earlier because we want to dump
            * the information to the configured log, not to the stderr. */
            // if (_config && cmd_ln_str_r(_ps->config, "-logfn")) {
            //     if (err_set_logfile(cmd_ln_str_r(_ps->config, "-logfn")) < 0) {
            //         E_ERROR("Cannot redirect log output\n");
            //         return -1;
            //     }
            // }
            
            _ps->mfclogdir = cmd_ln_str_r(_ps->config, "-mfclogdir");
            _ps->rawlogdir = cmd_ln_str_r(_ps->config, "-rawlogdir");
            _ps->senlogdir = cmd_ln_str_r(_ps->config, "-senlogdir");
            
            /* Fill in some default arguments. */
            //ps_expand_model_config(_ps);
            ps_expand_model_config(_ps);
            
            /* Free old searches (do this before other reinit) */
            ps_free_searches(_ps);
            _ps->searches = hash_table_new(3, HASH_CASE_YES);
            
            /* Free old acmod. */
            acmod_free(_ps->acmod);
            _ps->acmod = NULL;

            /* Free old dictionary (must be done after the two things above) */
            dict_free(_ps->dict);
            _ps->dict = NULL;

            /* Free d2p */
            dict2pid_free(_ps->d2p);
            _ps->d2p = NULL;

            

            /* Logmath computation (used in acmod and search) */
            if (_ps->lmath == NULL
                || (logmath_get_base(_ps->lmath) !=
                    (float64)cmd_ln_float32_r(_ps->config, "-logbase"))) {
                if (_ps->lmath)
                    logmath_free(_ps->lmath);
                _ps->lmath = logmath_init
                    ((float64)cmd_ln_float32_r(_ps->config, "-logbase"), 0,
                    cmd_ln_boolean_r(_ps->config, "-bestpath"));
            }

            
            
            
            /* Acoustic model (this is basically everything that
            * uttproc.c, senscr.c, and others used to do) */
            if ((_ps->acmod = acmod_init(_ps->config, _ps->lmath, NULL, NULL)) == NULL) //~6ms !
                return -1;
            
            
            //Batch scan change: (saves us 100 ms !!!!!)
            // if (cmd_ln_int32_r(_ps->config, "-pl_window") > 0) {
            //     /* Initialize an auxiliary phone loop search, which will run in
            //     * "parallel" with FSG or N-Gram search. */
            //     if ((_ps->phone_loop =
            //         phone_loop_search_init(_ps->config, _ps->acmod, _ps->dict)) == NULL)
            //         return -1;
            //     hash_table_enter(_ps->searches,
            //                     ps_search_name(_ps->phone_loop),
            //                     _ps->phone_loop);
            // }
            
            
            // /* Dictionary and triphone mappings (depends on acmod). */
            // /* FIXME: pass config, change arguments, implement LTS, etc. */
            // if ((_ps->dict = dict_init(_ps->config, _ps->acmod->mdef)) == NULL)
            //     return -1;
            
            // if ((_ps->d2p = dict2pid_build(_ps->acmod->mdef, _ps->dict)) == NULL) //~48-56ms !!!!!!!!!!!
            //     return -1;
            
            
            // lw = cmd_ln_float32_r(_ps->config, "-lw");
            
            // /* Determine whether we are starting out in FSG or N-Gram search mode.
            // * If neither is used skip search initialization. */

            // /* Load KWS if one was specified in config */
            // if ((keyphrase = cmd_ln_str_r(_ps->config, "-keyphrase"))) {
            //     if (ps_set_keyphrase(_ps, PS_DEFAULT_SEARCH, keyphrase))
            //         return -1;
            //     ps_set_search(_ps, PS_DEFAULT_SEARCH);
            // }
            
            // if ((path = cmd_ln_str_r(_ps->config, "-kws"))) {
            //     if (ps_set_kws(_ps, PS_DEFAULT_SEARCH, path))
            //         return -1;
            //     ps_set_search(_ps, PS_DEFAULT_SEARCH);
            // }
            
            // /* Load an FSG if one was specified in config */
            // if ((path = cmd_ln_str_r(_ps->config, "-fsg"))) {
            //     fsg_model_t *fsg = fsg_model_readfile(path, _ps->lmath, lw);
            //     if (!fsg)
            //         return -1;
            //     if (ps_set_fsg(_ps, PS_DEFAULT_SEARCH, fsg)) {
            //         fsg_model_free(fsg);
            //         return -1;
            //     }
            //     fsg_model_free(fsg);
            //     ps_set_search(_ps, PS_DEFAULT_SEARCH);
            // }
            
            // /* Or load a JSGF grammar */
            // if ((path = cmd_ln_str_r(_ps->config, "-jsgf"))) {
            //     // if (ps_set_jsgf_file(ps, PS_DEFAULT_SEARCH, path)
            //     //     || ps_set_search(ps, PS_DEFAULT_SEARCH))
            //     //     return -1;
            //     if (ps_set_jsgf_from_buffer(_ps, PS_DEFAULT_SEARCH, path, NULL, 0)
            //         || ps_set_search(_ps, PS_DEFAULT_SEARCH))
            //         return -1;
            // }
           

            // if ((path = cmd_ln_str_r(_ps->config, "-allphone"))) {
            //     if (ps_set_allphone_file(_ps, PS_DEFAULT_SEARCH, path)
            //             || ps_set_search(_ps, PS_DEFAULT_SEARCH))
            //             return -1;
            // }
            
            // if ((path = cmd_ln_str_r(_ps->config, "-lm")) && 
            //     !cmd_ln_boolean_r(_ps->config, "-allphone")) {
                
            //     if (ps_set_lm_file(_ps, PS_DEFAULT_SEARCH, path) //~47-51ms !!!!!!!!
            //         || ps_set_search(_ps, PS_DEFAULT_SEARCH))    //need both calls, otherwise cmninit becomes 40,3,-1 (default)
            //         return -1;
            // }
            
            //start=high_resolution_clock::now();
            

            if ((path = cmd_ln_str_r(_ps->config, "-lmctl"))) {
                const char *name;
                ngram_model_t *lmset;
                ngram_model_set_iter_t *lmset_it;

                if (!(lmset = ngram_model_set_read(_ps->config, path, _ps->lmath))) {
                    E_ERROR("Failed to read language model control file: %s\n", path);
                    return -1;
                }

                for(lmset_it = ngram_model_set_iter(lmset);
                    lmset_it; lmset_it = ngram_model_set_iter_next(lmset_it)) {    
                    ngram_model_t *lm = ngram_model_set_iter_model(lmset_it, &name);            
                    E_INFO("adding search %s\n", name);
                    if (ps_set_lm(_ps, name, lm)) {
                        ngram_model_set_iter_free(lmset_it);
                    ngram_model_free(lmset);
                        return -1;
                    }
                }
                ngram_model_free(lmset);

                name = cmd_ln_str_r(_ps->config, "-lmname");
                if (name)
                    ps_set_search(_ps, name);
                else {
                    E_ERROR("No default LM name (-lmname) for `-lmctl'\n");
                    return -1;
                }
            }
            
            /* Initialize performance timer. */
            _ps->perf.name = "decode";
            ptmr_init(&_ps->perf);
            //end=high_resolution_clock::now();
             //NEED TO KNOW WHAT CONSUMES THE MOST:
            // auto dur_ms = duration<double, std::milli>(end - start).count();
            // printf("\t\t\t\t %lfms\n", dur_ms);

            return 0;
        }

        static void
        ps_expand_model_config(ps_decoder_t *ps)
        {
            char const *hmmdir, *featparams;

            /* Disable memory mapping on Blackfin (FIXME: should be uClinux in general). */
        #ifdef __ADSPBLACKFIN__
            E_INFO("Will not use mmap() on uClinux/Blackfin.");
            cmd_ln_set_boolean_r(ps->config, "-mmap", FALSE);
        #endif

            /* Get acoustic model filenames and add them to the command-line */
            hmmdir = cmd_ln_str_r(ps->config, "-hmm");
            ps_expand_file_config(ps, "-mdef", "_mdef", hmmdir, "mdef");
            ps_expand_file_config(ps, "-mean", "_mean", hmmdir, "means");
            ps_expand_file_config(ps, "-var", "_var", hmmdir, "variances");
            ps_expand_file_config(ps, "-tmat", "_tmat", hmmdir, "transition_matrices");
            ps_expand_file_config(ps, "-mixw", "_mixw", hmmdir, "mixture_weights");
            ps_expand_file_config(ps, "-sendump", "_sendump", hmmdir, "sendump");
            ps_expand_file_config(ps, "-fdict", "_fdict", hmmdir, "noisedict");
            ps_expand_file_config(ps, "-lda", "_lda", hmmdir, "feature_transform");
            ps_expand_file_config(ps, "-featparams", "_featparams", hmmdir, "feat.params");
            ps_expand_file_config(ps, "-senmgau", "_senmgau", hmmdir, "senmgau");

            /* Look for feat.params in acoustic model dir. */
            if ((featparams = cmd_ln_str_r(ps->config, "_featparams"))) {
                if (NULL !=
                    cmd_ln_parse_file_r(ps->config, _feat_defn, featparams, FALSE))
                    E_INFO("Parsed model-specific feature parameters from %s\n",
                            featparams);
            }

            /* Print here because acmod_init might load feat.params file */
            if (err_get_logfp() != NULL) {
            //cmd_ln_print_values_r(ps->config, err_get_logfp(), ps_args());
            }
        }           

        static void
        ps_free_searches(ps_decoder_t *ps)
        {
            if (ps->searches) {
                hash_iter_t *search_it;
                for (search_it = hash_table_iter(ps->searches); search_it;
                    search_it = hash_table_iter_next(search_it)) {
                    ps_search_free((ps_search_t*)hash_entry_val(search_it->ent));
                }
                hash_table_free(ps->searches);
            }

            ps->searches = NULL;
            ps->search = NULL;
        }

        static void
        ps_expand_file_config(ps_decoder_t *ps, const char *arg, const char *extra_arg,
                        const char *hmmdir, const char *file)
        {
            const char *val;
            if ((val = cmd_ln_str_r(ps->config, arg)) != NULL) {
            cmd_ln_set_str_extra_r(ps->config, extra_arg, val);
            } else if (hmmdir == NULL) {
                cmd_ln_set_str_extra_r(ps->config, extra_arg, NULL);
            } else {
                char *tmp = string_join(hmmdir, "/", file, NULL);
                if (file_exists(tmp))
                    cmd_ln_set_str_extra_r(ps->config, extra_arg, tmp);
                else
                    cmd_ln_set_str_extra_r(ps->config, extra_arg, NULL);
                ckd_free(tmp);
            }
        } 

        static int
        file_exists(const char *path)
        {
            FILE *tmp;

            tmp = fopen(path, "rb");
            if (tmp) fclose(tmp);
            return (tmp != NULL);
        }               




};//class XYZ_Batch