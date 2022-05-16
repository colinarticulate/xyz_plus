//Inspired by:
//https://sourceforge.net/p/cmusphinx/discussion/help/thread/a445aa5c/
//----------------------------------------------------- pocketsphinx.c
/* System headers. */
#include <stdio.h>
#include <assert.h>
#include <chrono>
#include <memory>
//for testing:
#include <random>
#include <time.h>

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

/* SphinxBase headers. */
#include <xyzsphinxbase/err.h>
#include <xyzsphinxbase/strfuncs.h>
#include <xyzsphinxbase/filename.h>
#include <xyzsphinxbase/pio.h>
#include <xyzsphinxbase/jsgf.h>
#include <xyzsphinxbase/hash_table.h>
#include <xyzsphinxbase/byteorder.h>

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

//#include "dict.h"

//Re-doing API:
#include "_genrand.h"
#include "_fe_warp.h"
#include "_ps_error.h"

/*Denting API*/
//ps:
#include "ms_mgau.h"
#include "ptm_mgau.h"
#include "s2_semi_mgau.h"
//sb:
#include "fe_internal.h"
#include "fe_warp.h"
#include "fe_warp_inverse_linear.h"
#include "fe_warp_affine.h"
#include "fe_warp_piecewise_linear.h"
#include "fe_noise.h"
#include "fe_prespch_buf.h"
#include "fe_type.h"


//from fe_sigproc.c
/* Use extra precision for cosines, Hamming window, pre-emphasis
 * coefficient, twiddle factors. */
#ifdef FIXED_POINT
#define FLOAT2COS(x) FLOAT2FIX_ANY(x,30)
#define COSMUL(x,y) FIXMUL_ANY(x,y,30)
#else
#define FLOAT2COS(x) (x)
#define COSMUL(x,y) ((x)*(y))
#endif


#define MAX_INT32		((int32) 0x7fffffff)
#define MAX_N_FRAMES MAX_INT32


#define FEAT_DCEP_WIN		2
#define feat_n_stream(f) ((f)->n_stream)
#define feat_cepsize(f)		((f)->cepsize)
#define feat_stream_len(f,i) ((f)->stream_len[i])
#define feat_window_size(f) ((f)->window_size)
#define cep_dump_dbg(fcb,mfc,nfr,text)


/* Period parameters */
#define N 624
#define M 397
#define MATRIX_A 0x9908b0dfUL   /* constant vector a */
#define UPPER_MASK 0x80000000UL /* most significant w-r bits */
#define LOWER_MASK 0x7fffffffUL /* least significant r bits */




static const arg_t ps_args_def[114] = {
    POCKETSPHINX_OPTIONS,
    CMDLN_EMPTY_OPTION
};

/* Feature and front-end parameters that may be in feat.params */
static const arg_t feat_defn[40] = {
    waveform_to_cepstral_command_line_macro(),
    cepstral_to_feature_command_line_macro(),
    CMDLN_EMPTY_OPTION
};


//----------------------------------------------------
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <errno.h> 

#if defined(_WIN32) && !defined(__CYGWIN__)
#include <windows.h>
#else
#include <sys/select.h>
#endif

#include <xyzsphinxbase/err.h>    
#include <xyzsphinxbase/ad.h>

#include "pocketsphinx.h"

static const arg_t cont_args_def[119] = {
    POCKETSPHINX_OPTIONS,
    /* Argument file. */
    {"-argfile",
     ARG_STRING,
     NULL,
     "Argument file giving extra arguments."},
    {"-adcdev",
     ARG_STRING,
     NULL,
     "Name of audio device to use for input."},
    {"-infile",
     ARG_STRING,
     NULL,
     "Audio file to transcribe."},
    {"-inmic",
     ARG_BOOLEAN,
     "no",
     "Transcribe audio from microphone."},
    {"-time",
     ARG_BOOLEAN,
     "no",
     "Print word times in file transcription."},
    CMDLN_EMPTY_OPTION
};

static int
check_wav_header(char *header, int expected_sr)
{
    int sr;

    if (header[34] != 0x10) {
        printf("here\n");
        printf("Input audio file has [%d] bits per sample instead of 16\n", header[34]);
        E_ERROR("Input audio file has [%d] bits per sample instead of 16\n", header[34]);
        return 0;
    }
    if (header[20] != 0x1) {
        E_ERROR("Input audio file has compression [%d] and not required PCM\n", header[20]);
        return 0;
    }
    if (header[22] != 0x1) {
        E_ERROR("Input audio file has [%d] channels, expected single channel mono\n", header[22]);
        return 0;
    }
    sr = ((header[24] & 0xFF) | ((header[25] & 0xFF) << 8) | ((header[26] & 0xFF) << 16) | ((header[27] & 0xFF) << 24));
    if (sr != expected_sr) {
        E_ERROR("Input audio file has sample rate [%d], but decoder expects [%d]\n", sr, expected_sr);
        return 0;
    }
    return 1;
}

//Impossible to translate C error logging system in C++. This is really bad:
//Replaces E_FATAL macro:
#define _CONTINUOUS_E_FATAL(...)                            \
    do {                                                    \
        _pserror.err_msg(ERR_FATAL, FILELINE, __VA_ARGS__); \
        throw std::runtime_error(_pserror._msg);                  \
    } while (0)



class XYZ_PocketSphinx {
    private:
    
        ps_decoder_t *_ps;
        cmd_ln_t *_config;

        // char const *hyp, *uttid;
        // int16 buf[512];
        // int rv; 
        // int32 score;
 
        void* _jsgf_buffer; 
        size_t _jsgf_buffer_size;
        void* _audio_buffer;
        size_t _audio_buffer_size; 
        int _argc; 
        char **_argv;

        XYZ_SB_Genrand _genrand;
        XYZ_SB_FE_Warp _fe_warp;

        //Errors from pocketsphinx:
        PSErrorHandler _pserror;


            
    public:
        char _result[512];
        int _result_size;

        //float _cmn[13];

        //void XYZ_PocketPsphinx(void* jsgf_buffer, size_t jsgf_buffer_size, void* audio_buffer, size_t audio_buffer_size, int argc, char **argv) {
        void init(void* jsgf_buffer, size_t jsgf_buffer_size, void* audio_buffer, size_t audio_buffer_size, int argc, char **argv) {
            _jsgf_buffer = jsgf_buffer;
            _jsgf_buffer_size = jsgf_buffer_size;
            _audio_buffer = audio_buffer;
            _audio_buffer_size=audio_buffer_size;
            _argc=argc;
            //_argv=argv;

            // printf("%d\n",jsgf_buffer_size);
            // //printf("%d, %s sizeof: %lu\n",audio_buffer_size, (audio_buffer+34), sizeof((audio_buffer+34)));
            // //printf("%d\n",rsize);
            // printf("%d\n",argc);
            // printf("sizeof a char %lu\n",  sizeof(char));

            // for(int i=0;i<argc; i++){
            //         printf("%s\t\t%d\n",argv[i],strlen(argv[i]));
            //     }

            _argv = (char**)malloc(argc * sizeof(char*));
            for(int i =0; i< argc; i++){
                if(argv[i]!=NULL) {
                    _argv[i]=(char*)malloc(sizeof(char)*strlen(argv[i])+1);
                    strcpy(_argv[i],argv[i]);
                } else {
                    printf("Error: copying parameters.");
                }
            }
            //printf("%d\n",jsgf_buffer_size);

            // memset(_result,'a',sizeof(char)*512);
            // _result[511]='\0';
            // _result_size = 512;

            //printf("debugging");

        }


        int init_recognition(){
            char const *cfg;

            _config = cmd_ln_parse_r(NULL, cont_args_def, _argc, _argv, TRUE);

            
                /* Handle argument file as -argfile. */
            if (_config && (cfg = cmd_ln_str_r(_config, "-argfile")) != NULL) {
                _config = cmd_ln_parse_file_r(_config, cont_args_def, cfg, FALSE);
            }

            if (_config == NULL || (cmd_ln_str_r(_config, "-infile") == NULL && cmd_ln_boolean_r(_config, "-inmic") == FALSE)) {
                E_INFO("Specify '-infile <file.wav>' to recognize from file or '-inmic yes' to recognize from microphone.\n");
                cmd_ln_free_r(_config);
                
                return 1;
            }

            ps_default_search_args(_config);

            //_ps = ps_init_buffered(_config, _jsgf_buffer, _jsgf_buffer_size);
            ps_plus_init();

            if (_ps == NULL) {
                cmd_ln_free_r(_config);
                
                return 1;
            }

            //E_INFO("%s COMPILED ON: %s, AT: %s\n\n", _argv[0], __DATE__, __TIME__);
            if(_config==NULL) printf("config is NULL !!!!\n");
            if(_ps==NULL) printf("ps is NULL!!!\n");

                       
            return 0;
        }


        void terminate(){
            ps_free(_ps);
            cmd_ln_free_r(_config);
            
            
            for(int i =0; i< _argc; i++){
                free(_argv[i]);
            }

            free(_argv);
        }


        int retrieve_results(void){

            char buffer[256];
            buffer[0]='\0';
            _result[0]='\0';
            /* Log a backtrace if requested. */
            if (cmd_ln_boolean_r(_config, "-backtrace")) {
                // FILE *fresult=NULL;
                // fresult=fopen("result.txt","w");
                // if (fresult==NULL){
                //     printf("Couldn't open file for results.");
                // }

                ps_seg_t *seg;
                int32 score;

                const char *hyp = ps_get_hyp(_ps, &score);

                if (hyp != NULL) {
                    //E_INFO("%s (%d)\n", hyp, score);
                    sprintf(buffer, "%s*%d*", hyp, score);
                    strcat(_result, buffer);
                    //E_INFO_NOFN("%-20s %-5s %-5s\n", "word", "start", "end");

                    // fprintf(fresult, "%s (%d)\n", hyp, score);
                    // fflush(fresult);
                    // fprintf(fresult, "%-20s %-5s %-5s\n", "word", "start", "end");
                    // fflush(fresult);


                    for ( seg = ps_seg_iter(_ps); seg; seg = ps_seg_next(seg) ) {
                        int sf, ef;
                        char const *word = ps_seg_word(seg);
                        ps_seg_frames(seg, &sf, &ef);
                        //E_INFO_NOFN("%-20s %-5d %-5d\n", word, sf, ef);
                        //printf("%-20s %-5d %-5d\n", word, sf, ef);
                        //strcpy(buffer,word);
                        if (sf!=ef) { //for some obscure reason this if (meant to discard (NULL) entries) breaks the hash table when ps gets free.
                        
                            //fprintf(fresult, "%-20s %-5d %-5d\n", word, sf, ef);
                            sprintf(buffer, "%s,%d,%-d*", word, sf, ef);
                            strcat(_result, buffer);
                            //printf("%s\n", sresult);
                            
                            //fflush(fresult);
                        }
                    }
                    strcat(_result,"*");
                }
                
                //err=fclose(fresult);

                // if(fclose(fresult) != 0)
                // {
                //     fprintf(stderr, "Error closing file: %s", strerror(errno));
                // }



            } 

            return strlen(_result);
        }

       
        void recognize_from_buffered_file(){
            int16 adbuf[2048];
            const char *fname;
            const char *hyp;
            int32 k;
            uint8 utt_started, in_speech;
            //int32 print_times = cmd_ln_boolean_r(_config, "-time");
            int result_size=0;


            fname = cmd_ln_str_r(_config, "-infile");
            // if ((rawfd = fopen(fname, "rb")) == NULL) {
            //     _CONTINUOUS_E_FATAL_SYSTEM("Failed to open file '%s' for reading",
            //                    fname);
            // }
            FILE* file = NULL;
            file = fmemopen(_audio_buffer, _audio_buffer_size ,"rb");
            // FILE* fresult = NULL;
            // fresult = fopen("./result.txt","w");
            // if (fresult == NULL ) {
            //     printf("Couldn't open file for results.\n");
            // }

            
            
            //------------------- Needs better checking for wav format -----------------------------------------
            if (strlen(fname) > 4 && strcmp(fname + strlen(fname) - 4, ".wav") == 0) {
                char waveheader[44];
                k=fread(waveheader, 1, 44, file); //warning:  ignoring return value of ‘fread’
               
                if (!check_wav_header(waveheader, (int)cmd_ln_float32_r(_config, "-samprate"))) {
                        
                        _CONTINUOUS_E_FATAL("Failed to process file '%s' due to format mismatch.\n", fname);
                        //throw std::runtime_error("on purpose.");
                        
                    }
                
            }
            // //TEST!!!!
            // int max = 10;
            // int min = 1;

            // auto output = min + (rand() % static_cast<int>(max - min + 1));
            // if (output < 5 ) {
            //     _CONTINUOUS_E_FATAL("Failed to process file '%s' due to format mismatch. --- %d\n", fname,output);
            //     //throw std::runtime_error(_pserror._msg);
            // }

            if (strlen(fname) > 4 && strcmp(fname + strlen(fname) - 4, ".mp3") == 0) {
                _CONTINUOUS_E_FATAL("Can not decode mp3 files, convert input file to WAV 16kHz 16-bit mono before decoding.\n");
            }
            
            //---------------------------------------------------------------------------------------------------
                       //--------------------------------------------------------------------------------------------------------------- (loop)
        //     ps_start_utt(_ps);
           
        //     utt_started = FALSE;
        //     //int loop = 0;
        //     while ((k = fread(adbuf, sizeof(int16), 2048, file)) > 0) {
        //         ps_process_raw(_ps, adbuf, k, FALSE, FALSE);
        //         in_speech = ps_get_in_speech(_ps);
        //         if (in_speech && !utt_started) {
        //             utt_started = TRUE;
        //         } 
        //         if (!in_speech && utt_started) {
        //             ps_end_utt(_ps);
        //             //hyp = ps_get_hyp(ps, NULL);
        //             _result_size = retrieve_results();
        //             // if (hyp != NULL)
        //             // printf("%s\n", hyp);
                    
        //             // if (print_times)
        //             // //print_word_times();
        //             // fflush(stdout);

        //             ps_start_utt(_ps);
        //             utt_started = FALSE;
        //         }
        //         //loop++;
        //     }
        //    // printf("loops: %d\n", loop);
        //     ps_end_utt(_ps);
        //     if (utt_started) {

        //         //hyp = ps_get_hyp(ps, NULL);
        //         _result_size = retrieve_results();
        //     //     if (hyp != NULL) {
        //     // 	    printf("%s\n", hyp);
        //     //         //fprintf(fresult, "%s\n", hyp);
        //     // 	    if (print_times) {
        //     // 		print_word_times();
        //     //     }
        //     // }
        //     }
        //     //fclose(fresult);
        //     //--------------------------------------------------------------------------------------------------------------- (loop)


            //---- in one go:   
            //ps_decode_raw(_ps, file, _audio_buffer_size); //~50ms !!!!!
            _ps_decode_raw(_ps, file, _audio_buffer_size);
            //decode_raw(file, (long)_audio_buffer_size);
            _result_size = retrieve_results();
            //----- in one go


            fclose(file);
            //return _result_size;
           // extract_cepstral_mean();

        }

        //  void extract_cepstral_mean() {
            
        //         cmn_t *cmn = _ps->acmod->fcb->cmn_struct;
        //         for (int i = 0; i < cmn->veclen; i++) {
        //             //sprintf(strnum, "%5.2f ", MFCC2FLOAT(cmn->cmn_mean[i]));
        //             _cmn[i] = MFCC2FLOAT(cmn->cmn_mean[i]);

        //         }
        // }

        long
        _ps_decode_raw(ps_decoder_t *ps, FILE *rawfh,
                    long maxsamps)
        {
            int16 *data;
            long total, pos, endpos;

            ps_start_stream(ps);
            ps_start_utt(ps);

            /* If this file is seekable or maxsamps is specified, then decode
            * the whole thing at once. */
            if (maxsamps != -1) {
                data = (int16*)ckd_calloc(maxsamps, sizeof(*data));
                total = fread(data, sizeof(*data), maxsamps, rawfh);
                _ps_process_raw(ps, data, total, FALSE, TRUE);
                ckd_free(data);
            } else if ((pos = ftell(rawfh)) >= 0) {
                fseek(rawfh, 0, SEEK_END);
                endpos = ftell(rawfh);
                fseek(rawfh, pos, SEEK_SET);
                maxsamps = endpos - pos;

                data = (int16*)ckd_calloc(maxsamps, sizeof(*data));
                total = fread(data, sizeof(*data), maxsamps, rawfh);
                _ps_process_raw(ps, data, total, FALSE, TRUE);
                ckd_free(data);
            } else {
                /* Otherwise decode it in a stream. */
                total = 0;
                while (!feof(rawfh)) {
                    int16 data[256];
                    size_t nread;

                    nread = fread(data, sizeof(*data), sizeof(data)/sizeof(*data), rawfh);
                    _ps_process_raw(ps, data, nread, FALSE, FALSE);
                    total += nread;
                }
            }
            ps_end_utt(ps);
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
                    if (_fe_process_frames(acmod->fe, inout_raw, inout_n_samps,
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
                if (_fe_process_frames(acmod->fe, inout_raw, inout_n_samps,
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

        //static int
        int
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
            if (_fe_process_frames(acmod->fe, NULL, inout_n_samps, NULL, &nfr, NULL) < 0)
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
            if (_fe_process_frames(acmod->fe, inout_raw, inout_n_samps, acmod->mfc_buf, &nfr, NULL) < 0) //<<--- This calculates mfcc's
                return -1;
            _fe_end_utt(acmod->fe, acmod->mfc_buf[nfr], &ntail); //<<--- Recalculates mfcc for the last frame
            nfr += ntail;

            cepptr = acmod->mfc_buf;
            nfr = _acmod_process_full_cep(acmod, &cepptr, &nfr);
            acmod->n_mfc_frame = 0;
            return nfr;
        }

        

        //static int
        int
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
                    _CONTINUOUS_E_FATAL("Batch processing can not process more than %d frames " \
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
        }

        int
        ps_plus_reinit(void) //(ps_decoder_t *ps, cmd_ln_t *config, void *buffer, size_t size) // buffer for jsgf grammar
        {
            using namespace std::chrono;
            high_resolution_clock::time_point start;
            high_resolution_clock::time_point end;


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
            if ((_ps->acmod = _acmod_init(_ps->config, _ps->lmath, NULL, NULL)) == NULL)
                return -1;



            if (cmd_ln_int32_r(_ps->config, "-pl_window") > 0) {
                /* Initialize an auxiliary phone loop search, which will run in
                * "parallel" with FSG or N-Gram search. */
                if ((_ps->phone_loop =
                    phone_loop_search_init(_ps->config, _ps->acmod, _ps->dict)) == NULL)
                    return -1;
                hash_table_enter(_ps->searches,
                                ps_search_name(_ps->phone_loop),
                                _ps->phone_loop);
            }

            /* Dictionary and triphone mappings (depends on acmod). */
            /* FIXME: pass config, change arguments, implement LTS, etc. */
            
            if ((_ps->dict = dict_init(_ps->config, _ps->acmod->mdef)) == NULL)
                return -1;
           // start=high_resolution_clock::now();
            if ((_ps->d2p = dict2pid_build(_ps->acmod->mdef, _ps->dict)) == NULL) //<<----- ~50ms !!!! huge, what is it doing???
                return -1;
           // end=high_resolution_clock::now();

            lw = cmd_ln_float32_r(_ps->config, "-lw");

            /* Determine whether we are starting out in FSG or N-Gram search mode.
            * If neither is used skip search initialization. */

            /* Load KWS if one was specified in config */
            if ((keyphrase = cmd_ln_str_r(_ps->config, "-keyphrase"))) {
                if (ps_set_keyphrase(_ps, PS_DEFAULT_SEARCH, keyphrase))
                    return -1;
                ps_set_search(_ps, PS_DEFAULT_SEARCH);
            }

            if ((path = cmd_ln_str_r(_ps->config, "-kws"))) {
                if (ps_set_kws(_ps, PS_DEFAULT_SEARCH, path))
                    return -1;
                ps_set_search(_ps, PS_DEFAULT_SEARCH);
            }

            /* Load an FSG if one was specified in config */
            if ((path = cmd_ln_str_r(_ps->config, "-fsg"))) {
                fsg_model_t *fsg = fsg_model_readfile(path, _ps->lmath, lw);
                if (!fsg)
                    return -1;
                if (ps_set_fsg(_ps, PS_DEFAULT_SEARCH, fsg)) {
                    fsg_model_free(fsg);
                    return -1;
                }
                fsg_model_free(fsg);
                ps_set_search(_ps, PS_DEFAULT_SEARCH);
            }
            
            /* Or load a JSGF grammar */
            if ((path = cmd_ln_str_r(_ps->config, "-jsgf"))) {
                // if (ps_set_jsgf_file(ps, PS_DEFAULT_SEARCH, path)
                //     || ps_set_search(ps, PS_DEFAULT_SEARCH))
                //     return -1;
                if (ps_set_jsgf_from_buffer(_ps, PS_DEFAULT_SEARCH, path, _jsgf_buffer, _jsgf_buffer_size)
                    || ps_set_search(_ps, PS_DEFAULT_SEARCH))
                    return -1;

                
            }

            if ((path = cmd_ln_str_r(_ps->config, "-allphone"))) {
                if (ps_set_allphone_file(_ps, PS_DEFAULT_SEARCH, path)
                        || ps_set_search(_ps, PS_DEFAULT_SEARCH))
                        return -1;
            }

            if ((path = cmd_ln_str_r(_ps->config, "-lm")) && 
                !cmd_ln_boolean_r(_ps->config, "-allphone")) {
                if (ps_set_lm_file(_ps, PS_DEFAULT_SEARCH, path)
                    || ps_set_search(_ps, PS_DEFAULT_SEARCH))
                    return -1;
            }

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
                    cmd_ln_parse_file_r(ps->config, feat_defn, featparams, FALSE))
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

        acmod_t *
        _acmod_init(cmd_ln_t *config, logmath_t *lmath, fe_t *fe, feat_t *fcb)
        {
            acmod_t *acmod;

            acmod = (acmod_t*)ckd_calloc(1, sizeof(*acmod));
            acmod->config = cmd_ln_retain(config);
            acmod->lmath = lmath;
            acmod->state = ACMOD_IDLE;

            /* Initialize feature computation. */
            if (fe) {
                if (_acmod_fe_mismatch(acmod, fe))
                    goto error_out;
                fe_retain(fe);
                acmod->fe = fe;
            }
            else {
                /* Initialize a new front end. */
                acmod->fe = _fe_init_auto_r(config);
                if (acmod->fe == NULL)
                    goto error_out;
                if (_acmod_fe_mismatch(acmod, acmod->fe))
                    goto error_out;
            }
            if (fcb) {
                if (_acmod_feat_mismatch(acmod, fcb))
                    goto error_out;
                feat_retain(fcb);
                acmod->fcb = fcb;
            }
            else {
                /* Initialize a new fcb. */
                if (_acmod_init_feat(acmod) < 0)
                    goto error_out;
            }

            /* Load acoustic model parameters. */
            if (_acmod_init_am(acmod) < 0)
                goto error_out;


            /* The MFCC buffer needs to be at least as large as the dynamic
            * feature window.  */
            acmod->n_mfc_alloc = acmod->fcb->window_size * 2 + 1;
            acmod->mfc_buf = (mfcc_t **)
                ckd_calloc_2d(acmod->n_mfc_alloc, acmod->fcb->cepsize,
                            sizeof(**acmod->mfc_buf));

            /* Feature buffer has to be at least as large as MFCC buffer. */
            acmod->n_feat_alloc = acmod->n_mfc_alloc + cmd_ln_int32_r(config, "-pl_window");
            acmod->feat_buf = feat_array_alloc(acmod->fcb, acmod->n_feat_alloc);
            acmod->framepos = (long int*)ckd_calloc(acmod->n_feat_alloc, sizeof(*acmod->framepos));

            acmod->utt_start_frame = 0;

            /* Senone computation stuff. */
            acmod->senone_scores = (int16*)ckd_calloc(bin_mdef_n_sen(acmod->mdef),
                                                            sizeof(*acmod->senone_scores));
            acmod->senone_active_vec = (bitvec_t*)bitvec_alloc(bin_mdef_n_sen(acmod->mdef));
            acmod->senone_active = (uint8*)ckd_calloc(bin_mdef_n_sen(acmod->mdef),
                                                            sizeof(*acmod->senone_active));
            acmod->log_zero = logmath_get_zero(acmod->lmath);
            acmod->compallsen = cmd_ln_boolean_r(config, "-compallsen");
            return acmod;

        error_out:
            acmod_free(acmod);
            return NULL;
        }   

        int
        _acmod_fe_mismatch(acmod_t *acmod, fe_t *fe)
        {
            /* Output vector dimension needs to be the same. */
            if (cmd_ln_int32_r(acmod->config, "-ceplen") != fe_get_output_size(fe)) {
                E_ERROR("Configured feature length %d doesn't match feature "
                        "extraction output size %d\n",
                        cmd_ln_int32_r(acmod->config, "-ceplen"),
                        fe_get_output_size(fe));
                return TRUE;
            }
            /* Feature parameters need to be the same. */
            /* ... */
            return FALSE;
        }

        int
        _acmod_feat_mismatch(acmod_t *acmod, feat_t *fcb)
        {
            /* Feature type needs to be the same. */
            if (0 != strcmp(cmd_ln_str_r(acmod->config, "-feat"), feat_name(fcb)))
                return TRUE;
            /* Input vector dimension needs to be the same. */
            if (cmd_ln_int32_r(acmod->config, "-ceplen") != feat_cepsize(fcb))
                return TRUE;
            /* FIXME: Need to check LDA and stuff too. */
            return FALSE;
        }

        static int
        _acmod_init_feat(acmod_t *acmod)
        {
            acmod->fcb =
                feat_init(cmd_ln_str_r(acmod->config, "-feat"),
                        cmn_type_from_str(cmd_ln_str_r(acmod->config,"-cmn")),
                        cmd_ln_boolean_r(acmod->config, "-varnorm"),
                        agc_type_from_str(cmd_ln_str_r(acmod->config, "-agc")),
                        1, cmd_ln_int32_r(acmod->config, "-ceplen"));
            if (acmod->fcb == NULL)
                return -1;

            if (cmd_ln_str_r(acmod->config, "_lda")) {
                E_INFO("Reading linear feature transformation from %s\n",
                    cmd_ln_str_r(acmod->config, "_lda"));
                if (feat_read_lda(acmod->fcb,
                                cmd_ln_str_r(acmod->config, "_lda"),
                                cmd_ln_int32_r(acmod->config, "-ldadim")) < 0)
                    return -1;
            }

            if (cmd_ln_str_r(acmod->config, "-svspec")) {
                int32 **subvecs;
                E_INFO("Using subvector specification %s\n",
                    cmd_ln_str_r(acmod->config, "-svspec"));
                if ((subvecs = parse_subvecs(cmd_ln_str_r(acmod->config, "-svspec"))) == NULL)
                    return -1;
                if ((feat_set_subvecs(acmod->fcb, subvecs)) < 0)
                    return -1;
            }

            if (cmd_ln_exists_r(acmod->config, "-agcthresh")
                && 0 != strcmp(cmd_ln_str_r(acmod->config, "-agc"), "none")) {
                agc_set_threshold(acmod->fcb->agc_struct,
                                cmd_ln_float32_r(acmod->config, "-agcthresh"));
            }

            if (acmod->fcb->cmn_struct
                && cmd_ln_exists_r(acmod->config, "-cmninit")) {
                char *c, *cc, *vallist;
                int32 nvals;

                vallist = ckd_salloc(cmd_ln_str_r(acmod->config, "-cmninit"));
                c = vallist;
                nvals = 0;
                while (nvals < acmod->fcb->cmn_struct->veclen
                    && (cc = strchr(c, ',')) != NULL) {
                    *cc = '\0';
                    acmod->fcb->cmn_struct->cmn_mean[nvals] = FLOAT2MFCC(atof_c(c));
                    c = cc + 1;
                    ++nvals;
                }
                if (nvals < acmod->fcb->cmn_struct->veclen && *c != '\0') {
                    acmod->fcb->cmn_struct->cmn_mean[nvals] = FLOAT2MFCC(atof_c(c));
                }
                ckd_free(vallist);
            }
            return 0;
        }

        static int
        _acmod_init_am(acmod_t *acmod)
        {
            char const *mdeffn, *tmatfn, *mllrfn, *hmmdir;

            /* Read model definition. */
            if ((mdeffn = cmd_ln_str_r(acmod->config, "_mdef")) == NULL) {
                if ((hmmdir = cmd_ln_str_r(acmod->config, "-hmm")) == NULL)
                    E_ERROR("Acoustic model definition is not specified either "
                            "with -mdef option or with -hmm\n");
                else
                    E_ERROR("Folder '%s' does not contain acoustic model "
                            "definition 'mdef'\n", hmmdir);

                return -1;
            }

            if ((acmod->mdef = bin_mdef_read(acmod->config, mdeffn)) == NULL) {
                E_ERROR("Failed to read acoustic model definition from %s\n", mdeffn);
                return -1;
            }

            /* Read transition matrices. */
            if ((tmatfn = cmd_ln_str_r(acmod->config, "_tmat")) == NULL) {
                E_ERROR("No tmat file specified\n");
                return -1;
            }
            acmod->tmat = tmat_init(tmatfn, acmod->lmath,
                                    cmd_ln_float32_r(acmod->config, "-tmatfloor"),
                                    TRUE);

            /* Read the acoustic models. */
            if ((cmd_ln_str_r(acmod->config, "_mean") == NULL)
                || (cmd_ln_str_r(acmod->config, "_var") == NULL)
                || (cmd_ln_str_r(acmod->config, "_tmat") == NULL)) {
                E_ERROR("No mean/var/tmat files specified\n");
                return -1;
            }

            if (cmd_ln_str_r(acmod->config, "_senmgau")) {
                E_INFO("Using general multi-stream GMM computation\n");
                acmod->mgau = ms_mgau_init(acmod, acmod->lmath, acmod->mdef);
                if (acmod->mgau == NULL)
                    return -1;
            }
            else {
                E_INFO("Attempting to use PTM computation module\n");
                if ((acmod->mgau = ptm_mgau_init(acmod, acmod->mdef)) == NULL) {
                    E_INFO("Attempting to use semi-continuous computation module\n");
                    if ((acmod->mgau = s2_semi_mgau_init(acmod)) == NULL) {
                        E_INFO("Falling back to general multi-stream GMM computation\n");
                        acmod->mgau = ms_mgau_init(acmod, acmod->lmath, acmod->mdef);
                        if (acmod->mgau == NULL) {
                            E_ERROR("Failed to read acoustic model\n");
                            return -1;
                        }
                    }
                }
            }

            /* If there is an MLLR transform, apply it. */
            if ((mllrfn = cmd_ln_str_r(acmod->config, "-mllr"))) {
                ps_mllr_t *mllr = ps_mllr_read(mllrfn);
                if (mllr == NULL)
                    return -1;
                acmod_update_mllr(acmod, mllr);
            }

            return 0;
        }

        fe_t *
        _fe_init_auto_r(cmd_ln_t *config)
        {
            fe_t *fe;
            int prespch_frame_len;

            fe = (fe_t*)ckd_calloc(1, sizeof(*fe));
            fe->refcount = 1;

            /* transfer params to front end */
            if (_fe_parse_general_params(cmd_ln_retain(config), fe) < 0) {
                fe_free(fe);
                return NULL;
            }

            /* compute remaining fe parameters */
            /* We add 0.5 so approximate the float with the closest
            * integer. E.g., 2.3 is truncate to 2, whereas 3.7 becomes 4
            */
            fe->frame_shift = (int32) (fe->sampling_rate / fe->frame_rate + 0.5);
            fe->frame_size = (int32) (fe->window_length * fe->sampling_rate + 0.5);
            fe->pre_emphasis_prior = 0;
            
            fe_start_stream(fe);

            assert (fe->frame_shift > 1);

            if (fe->frame_size < fe->frame_shift) {
                E_ERROR
                    ("Frame size %d (-wlen) must be greater than frame shift %d (-frate)\n",
                    fe->frame_size, fe->frame_shift);
                fe_free(fe);
                return NULL;
            }


            if (fe->frame_size > (fe->fft_size)) {
                E_ERROR
                    ("Number of FFT points has to be a power of 2 higher than %d, it is %d\n",
                    fe->frame_size, fe->fft_size);
                fe_free(fe);
                return NULL;
            }

            if (fe->dither)
                _fe_init_dither(fe->dither_seed); //static variable dependency

            /* establish buffers for overflow samps and hamming window */
            fe->overflow_samps = (int16*)ckd_calloc(fe->frame_size, sizeof(int16));
            fe->hamming_window = (window_t*)ckd_calloc(fe->frame_size/2, sizeof(window_t));

            /* create hamming window */
            fe_create_hamming(fe->hamming_window, fe->frame_size);

            /* init and fill appropriate filter structure */
            fe->mel_fb = (melfb_t*)ckd_calloc(1, sizeof(*fe->mel_fb));

            /* transfer params to mel fb */
            _fe_parse_melfb_params(config, fe, fe->mel_fb);
            
            if (fe->mel_fb->upper_filt_freq > fe->sampling_rate / 2 + 1.0) {
            E_ERROR("Upper frequency %.1f is higher than samprate/2 (%.1f)\n", 
                fe->mel_fb->upper_filt_freq, fe->sampling_rate / 2);
            fe_free(fe);
            return NULL;
            }
            
            fe_build_melfilters(fe->mel_fb);

            fe_compute_melcosine(fe->mel_fb);
            if (fe->remove_noise || fe->remove_silence)
                fe->noise_stats = fe_init_noisestats(fe->mel_fb->num_filters);

            fe->vad_data = (vad_data_t*)ckd_calloc(1, sizeof(*fe->vad_data));
            prespch_frame_len = fe->log_spec != RAW_LOG_SPEC ? fe->num_cepstra : fe->mel_fb->num_filters;
            fe->vad_data->prespch_buf = fe_prespch_init(fe->pre_speech + 1, prespch_frame_len, fe->frame_shift);

            /* Create temporary FFT, spectrum and mel-spectrum buffers. */
            /* FIXME: Gosh there are a lot of these. */
            fe->spch = (int16*)ckd_calloc(fe->frame_size, sizeof(*fe->spch));
            fe->frame = (frame_t*)ckd_calloc(fe->fft_size, sizeof(*fe->frame));
            fe->spec = (powspec_t*)ckd_calloc(fe->fft_size, sizeof(*fe->spec));
            fe->mfspec = (powspec_t*)ckd_calloc(fe->mel_fb->num_filters, sizeof(*fe->mfspec));

            /* create twiddle factors */
            fe->ccc = (frame_t*)ckd_calloc(fe->fft_size / 4, sizeof(*fe->ccc));
            fe->sss = (frame_t*)ckd_calloc(fe->fft_size / 4, sizeof(*fe->sss));
            fe_create_twiddle(fe);

            if (cmd_ln_boolean_r(config, "-verbose")) {
                _fe_print_current(fe);
            }

            /*** Initialize the overflow buffers ***/
            fe_start_utt(fe);
            return fe;
        }   

        void
        _fe_init_dither(int32 seed)
        {
            E_INFO("Using %d as the seed.\n", seed);
            //s3_rand_seed(seed);//just a macro for:
            _genrand.init_genrand(seed);
        }

        //static int
        int
        _fe_parse_melfb_params(cmd_ln_t *config, fe_t *fe, melfb_t * mel)
        {
            mel->sampling_rate = fe->sampling_rate;
            mel->fft_size = fe->fft_size;
            mel->num_cepstra = fe->num_cepstra;
            mel->num_filters = cmd_ln_int32_r(config, "-nfilt");

            if (fe->log_spec)
                fe->feature_dimension = mel->num_filters;
            else
                fe->feature_dimension = fe->num_cepstra;

            mel->upper_filt_freq = cmd_ln_float32_r(config, "-upperf");
            mel->lower_filt_freq = cmd_ln_float32_r(config, "-lowerf");

            mel->doublewide = cmd_ln_boolean_r(config, "-doublebw");

            mel->warp_type = cmd_ln_str_r(config, "-warp_type");
            mel->warp_params = cmd_ln_str_r(config, "-warp_params");
            mel->lifter_val = cmd_ln_int32_r(config, "-lifter");

            mel->unit_area = cmd_ln_boolean_r(config, "-unit_area");
            mel->round_filters = cmd_ln_boolean_r(config, "-round_filters");

            if (_fe_warp.fe_warp_set(mel, mel->warp_type) != FE_SUCCESS) {
                E_ERROR("Failed to initialize the warping function.\n");
                return -1;
            }
            _fe_warp.fe_warp_set_parameters(mel, mel->warp_params, mel->sampling_rate);
            return 0;
        }

        
  
        int
        _fe_parse_general_params(cmd_ln_t *config, fe_t * fe)
        {
            int j, frate;

            fe->config = config;
            fe->sampling_rate = cmd_ln_float32_r(config, "-samprate");
            frate = cmd_ln_int32_r(config, "-frate");
            if (frate > MAX_INT16 || frate > fe->sampling_rate || frate < 1) {
                E_ERROR
                    ("Frame rate %d can not be bigger than sample rate %.02f\n",
                    frate, fe->sampling_rate);
                return -1;
            }

            fe->frame_rate = (int16)frate;
            if (cmd_ln_boolean_r(config, "-dither")) {
                fe->dither = 1;
                fe->dither_seed = cmd_ln_int32_r(config, "-seed");
            }
        #ifdef WORDS_BIGENDIAN
            fe->swap = strcmp("big", cmd_ln_str_r(config, "-input_endian")) == 0 ? 0 : 1;
        #else        
            fe->swap = strcmp("little", cmd_ln_str_r(config, "-input_endian")) == 0 ? 0 : 1;
        #endif
            fe->window_length = cmd_ln_float32_r(config, "-wlen");
            fe->pre_emphasis_alpha = cmd_ln_float32_r(config, "-alpha");

            fe->num_cepstra = (uint8)cmd_ln_int32_r(config, "-ncep");
            fe->fft_size = (int16)cmd_ln_int32_r(config, "-nfft");

            /* Check FFT size, compute FFT order (log_2(n)) */
            for (j = fe->fft_size, fe->fft_order = 0; j > 1; j >>= 1, fe->fft_order++) {
                if (((j % 2) != 0) || (fe->fft_size <= 0)) {
                    E_ERROR("fft: number of points must be a power of 2 (is %d)\n",
                            fe->fft_size);
                    return -1;
                }
            }
            /* Verify that FFT size is greater or equal to window length. */
            if (fe->fft_size < (int)(fe->window_length * fe->sampling_rate)) {
                E_ERROR("FFT: Number of points must be greater or equal to frame size (%d samples)\n",
                        (int)(fe->window_length * fe->sampling_rate));
                return -1;
            }

            fe->pre_speech = (int16)cmd_ln_int32_r(config, "-vad_prespeech");
            fe->post_speech = (int16)cmd_ln_int32_r(config, "-vad_postspeech");
            fe->start_speech = (int16)cmd_ln_int32_r(config, "-vad_startspeech");
            fe->vad_threshold = cmd_ln_float32_r(config, "-vad_threshold");

            fe->remove_dc = cmd_ln_boolean_r(config, "-remove_dc");
            fe->remove_noise = cmd_ln_boolean_r(config, "-remove_noise");
            fe->remove_silence = cmd_ln_boolean_r(config, "-remove_silence");

            if (0 == strcmp(cmd_ln_str_r(config, "-transform"), "dct"))
                fe->transform = DCT_II;
            else if (0 == strcmp(cmd_ln_str_r(config, "-transform"), "legacy"))
                fe->transform = LEGACY_DCT;
            else if (0 == strcmp(cmd_ln_str_r(config, "-transform"), "htk"))
                fe->transform = DCT_HTK;
            else {
                E_ERROR("Invalid transform type (values are 'dct', 'legacy', 'htk')\n");
                return -1;
            }

            if (cmd_ln_boolean_r(config, "-logspec"))
                fe->log_spec = RAW_LOG_SPEC;
            if (cmd_ln_boolean_r(config, "-smoothspec"))
                fe->log_spec = SMOOTH_LOG_SPEC;

            return 0;
        }

        void
        _fe_print_current(fe_t const *fe)
        {
            E_INFO("Current FE Parameters:\n");
            E_INFO("\tSampling Rate:             %f\n", fe->sampling_rate);
            E_INFO("\tFrame Size:                %d\n", fe->frame_size);
            E_INFO("\tFrame Shift:               %d\n", fe->frame_shift);
            E_INFO("\tFFT Size:                  %d\n", fe->fft_size);
            E_INFO("\tLower Frequency:           %g\n",
                fe->mel_fb->lower_filt_freq);
            E_INFO("\tUpper Frequency:           %g\n",
                fe->mel_fb->upper_filt_freq);
            E_INFO("\tNumber of filters:         %d\n", fe->mel_fb->num_filters);
            E_INFO("\tNumber of Overflow Samps:  %d\n", fe->num_overflow_samps);
            E_INFO("Will %sremove DC offset at frame level\n",
                fe->remove_dc ? "" : "not ");
            if (fe->dither) {
                E_INFO("Will add dither to audio\n");
                E_INFO("Dither seeded with %d\n", fe->dither_seed);
            }
            else {
                E_INFO("Will not add dither to audio\n");
            }
            if (fe->mel_fb->lifter_val) {
                E_INFO("Will apply sine-curve liftering, period %d\n",
                    fe->mel_fb->lifter_val);
            }
            E_INFO("Will %snormalize filters to unit area\n",
                fe->mel_fb->unit_area ? "" : "not ");
            E_INFO("Will %sround filter frequencies to DFT points\n",
                fe->mel_fb->round_filters ? "" : "not ");
            E_INFO("Will %suse double bandwidth in mel filter\n",
                fe->mel_fb->doublewide ? "" : "not ");
        }



        prespch_buf_t *
        _fe_prespch_init(int num_frames, int num_cepstra, int num_samples)
        {
            prespch_buf_t *prespch_buf; 

            std::shared_ptr<prespch_buf_t> prespch_buf_ptr; //https://stackoverflow.com/questions/44963201/when-does-an-incomplete-type-error-occur-in-c

            prespch_buf = (prespch_buf_t*) ckd_calloc(1, sizeof(std::shared_ptr<prespch_buf_t>));

            prespch_buf->num_cepstra = num_cepstra;
            prespch_buf_ptr->num_frames_cep = num_frames;
            prespch_buf_ptr->num_samples = num_samples;
            prespch_buf_ptr->num_frames_pcm = 0;

            prespch_buf_ptr->cep_write_ptr = 0;
            prespch_buf_ptr->cep_read_ptr = 0;
            prespch_buf_ptr->ncep = 0;
            
            prespch_buf_ptr->pcm_write_ptr = 0;
            prespch_buf_ptr->pcm_read_ptr = 0;
            prespch_buf_ptr->npcm = 0;

            prespch_buf_ptr->cep_buf = (mfcc_t **)
                ckd_calloc_2d(num_frames, num_cepstra,
                            sizeof(**prespch_buf_ptr->cep_buf));

            prespch_buf_ptr->pcm_buf = (int16 *)
                ckd_calloc(prespch_buf_ptr->num_frames_pcm * prespch_buf_ptr->num_samples,
                        sizeof(int16));

            return prespch_buf;
        }


        int
        _fe_process_frames(fe_t *fe,
                        int16 const **inout_spch,
                        size_t *inout_nsamps,
                        mfcc_t **buf_cep,
                        int32 *inout_nframes,
                        int32 *out_frameidx)
        {
            return _fe_process_frames_ext(fe, inout_spch, inout_nsamps, buf_cep, inout_nframes, NULL, NULL, out_frameidx);
        }


        int 
        _fe_process_frames_ext(fe_t *fe,
                        int16 const **inout_spch,
                        size_t *inout_nsamps,
                        mfcc_t **buf_cep,
                        int32 *inout_nframes,
                        int16 *voiced_spch,
                        int32 *voiced_spch_nsamps,
                        int32 *out_frameidx)
        {
            int outidx, n_overflow, orig_n_overflow;
            int16 const *orig_spch;
            size_t orig_nsamps;
            
            /* The logic here is pretty complex, please be careful with modifications */

            /* FIXME: Dump PCM data if needed */

            /* In the special case where there is no output buffer, return the
            * maximum number of frames which would be generated. */
            if (buf_cep == NULL) {
                if (*inout_nsamps + fe->num_overflow_samps < (size_t)fe->frame_size)
                    *inout_nframes = 0;
                else 
                    *inout_nframes = 1
                        + ((*inout_nsamps + fe->num_overflow_samps - fe->frame_size)
                        / fe->frame_shift);
                if (!fe->vad_data->in_speech)
                    *inout_nframes += fe_prespch_ncep(fe->vad_data->prespch_buf);
                return *inout_nframes;
            }

            if (out_frameidx)
                *out_frameidx = 0;

            /* Are there not enough samples to make at least 1 frame? */
            if (*inout_nsamps + fe->num_overflow_samps < (size_t)fe->frame_size) {
                if (*inout_nsamps > 0) {
                    /* Append them to the overflow buffer. */
                    memcpy(fe->overflow_samps + fe->num_overflow_samps,
                        *inout_spch, *inout_nsamps * (sizeof(int16)));
                    fe->num_overflow_samps += *inout_nsamps;
                fe->num_processed_samps += *inout_nsamps;
                    *inout_spch += *inout_nsamps;
                    *inout_nsamps = 0;
                }
                /* We produced no frames of output, sorry! */
                *inout_nframes = 0;
                return 0;
            }

            /* Can't write a frame?  Then do nothing! */
            if (*inout_nframes < 1) {
                *inout_nframes = 0;
                return 0;
            }

            /* Index of output frame. */
            outidx = 0;

            /* Try to read from prespeech buffer */
            if (fe->vad_data->in_speech && fe_prespch_ncep(fe->vad_data->prespch_buf) > 0) {
                outidx = _fe_copy_from_prespch(fe, inout_nframes, buf_cep, outidx);
                if ((*inout_nframes) < 1) {
                    /* mfcc buffer is filled from prespeech buffer */
                    *inout_nframes = outidx;
                    return 0;
                }
            }

            /* Keep track of the original start of the buffer. */
            orig_spch = *inout_spch;
            orig_nsamps = *inout_nsamps;
            orig_n_overflow = fe->num_overflow_samps;

            /* Start processing, taking care of any incoming overflow. */
            if (fe->num_overflow_samps > 0) {
                int offset = fe->frame_size - fe->num_overflow_samps;
                /* Append start of spch to overflow samples to make a full frame. */
                memcpy(fe->overflow_samps + fe->num_overflow_samps,
                    *inout_spch, offset * sizeof(**inout_spch));
                _fe_read_frame(fe, fe->overflow_samps, fe->frame_size);
                /* Update input-output pointers and counters. */
                *inout_spch += offset;
                *inout_nsamps -= offset;
            } else {
                _fe_read_frame(fe, *inout_spch, fe->frame_size);
                /* Update input-output pointers and counters. */
                *inout_spch += fe->frame_size;
                *inout_nsamps -= fe->frame_size;
            }

            fe_write_frame(fe, buf_cep[outidx], voiced_spch != NULL);
            outidx = _fe_check_prespeech(fe, inout_nframes, buf_cep, outidx, out_frameidx, inout_nsamps, orig_nsamps);

            /* Process all remaining frames. */
            while (*inout_nframes > 0 && *inout_nsamps >= (size_t)fe->frame_shift) {
                _fe_shift_frame(fe, *inout_spch, fe->frame_shift);
                fe_write_frame(fe, buf_cep[outidx], voiced_spch != NULL);

            outidx = _fe_check_prespeech(fe, inout_nframes, buf_cep, outidx, out_frameidx, inout_nsamps, orig_nsamps);

                /* Update input-output pointers and counters. */
                *inout_spch += fe->frame_shift;
                *inout_nsamps -= fe->frame_shift;
            }

            /* How many relevant overflow samples are there left? */
            if (fe->num_overflow_samps <= 0) {
                /* Maximum number of overflow samples past *inout_spch to save. */
                n_overflow = *inout_nsamps;
                if (n_overflow > fe->frame_shift)
                    n_overflow = fe->frame_shift;
                fe->num_overflow_samps = fe->frame_size - fe->frame_shift;
                /* Make sure this isn't an illegal read! */
                if (fe->num_overflow_samps > *inout_spch - orig_spch)
                    fe->num_overflow_samps = *inout_spch - orig_spch;
                fe->num_overflow_samps += n_overflow;
                if (fe->num_overflow_samps > 0) {
                    memcpy(fe->overflow_samps,
                        *inout_spch - (fe->frame_size - fe->frame_shift),
                        fe->num_overflow_samps * sizeof(**inout_spch));
                    /* Update the input pointer to cover this stuff. */
                    *inout_spch += n_overflow;
                    *inout_nsamps -= n_overflow;
                }
            } else {
                /* There is still some relevant data left in the overflow buffer. */
                /* Shift existing data to the beginning. */
                memmove(fe->overflow_samps,
                        fe->overflow_samps + orig_n_overflow - fe->num_overflow_samps,
                        fe->num_overflow_samps * sizeof(*fe->overflow_samps));
                /* Copy in whatever we had in the original speech buffer. */
                n_overflow = *inout_spch - orig_spch + *inout_nsamps;
                if (n_overflow > fe->frame_size - fe->num_overflow_samps)
                    n_overflow = fe->frame_size - fe->num_overflow_samps;
                memcpy(fe->overflow_samps + fe->num_overflow_samps,
                    orig_spch, n_overflow * sizeof(*orig_spch));
                fe->num_overflow_samps += n_overflow;
                /* Advance the input pointers. */
                if (n_overflow > *inout_spch - orig_spch) {
                    n_overflow -= (*inout_spch - orig_spch);
                    *inout_spch += n_overflow;
                    *inout_nsamps -= n_overflow;
                }
            }

            /* Finally update the frame counter with the number of frames
            * and global sample counter with number of samples we procesed */
            *inout_nframes = outidx; /* FIXME: Not sure why I wrote it this way... */
            fe->num_processed_samps += orig_nsamps - *inout_nsamps;

            return 0;
        }

        /**
         * Copy frames collected in prespeech buffer
         */
        static int
        _fe_copy_from_prespch(fe_t *fe, int32 *inout_nframes, mfcc_t **buf_cep, int outidx)
        {
            while ((*inout_nframes) > 0 && fe_prespch_read_cep(fe->vad_data->prespch_buf, buf_cep[outidx]) > 0) {
                outidx++;
                    (*inout_nframes)--;
            }
            return outidx;    
        }

        /**
         * Update pointers after we processed a frame. A complex logic used in two places in fe_process_frames
         */
        static int
        _fe_check_prespeech(fe_t *fe, int32 *inout_nframes, mfcc_t **buf_cep, int outidx, int32 *out_frameidx, size_t *inout_nsamps, int orig_nsamps)
        {
            if (fe->vad_data->in_speech) {    
            if (fe_prespch_ncep(fe->vad_data->prespch_buf) > 0) {

                    /* Previous frame triggered vad into speech state. Last frame is in the end of 
                    prespeech buffer, so overwrite it */
                    outidx = _fe_copy_from_prespch(fe, inout_nframes, buf_cep, outidx);

                    /* Sets the start frame for the returned data so that caller can update timings */
                if (out_frameidx) {
                        *out_frameidx = (fe->num_processed_samps + orig_nsamps - *inout_nsamps) / fe->frame_shift - fe->pre_speech;
                    }
                } else {
                outidx++;
                    (*inout_nframes)--;
                }
            }
            /* Amount of data behind the original input which is still needed. */
            if (fe->num_overflow_samps > 0)
                fe->num_overflow_samps -= fe->frame_shift;

            return outidx;
        }


        int
        _fe_read_frame(fe_t * fe, int16 const *in, int32 len)
        {
            int i;

            if (len > fe->frame_size)
                len = fe->frame_size;

            /* Read it into the raw speech buffer. */
            memcpy(fe->spch, in, len * sizeof(*in));
            /* Swap and dither if necessary. */
            if (fe->swap)
                for (i = 0; i < len; ++i)
                    SWAP_INT16(&fe->spch[i]);
            if (fe->dither)
                for (i = 0; i < len; ++i)
                    //fe->spch[i] += (int16) ((!(s3_rand_int31() % 4)) ? 1 : 0); //Race condition 
                    fe->spch[i] += (int16) ((!(_genrand.genrand_int31() % 4)) ? 1 : 0); //Race condition

            return _fe_spch_to_frame(fe, len);
        }

        static int
        _fe_spch_to_frame(fe_t * fe, int len)
        {
            /* Copy to the frame buffer. */
            if (fe->pre_emphasis_alpha != 0.0) {
                _fe_pre_emphasis(fe->spch, fe->frame, len,
                                fe->pre_emphasis_alpha, fe->pre_emphasis_prior);
                if (len >= fe->frame_shift)
                    fe->pre_emphasis_prior = fe->spch[fe->frame_shift - 1];
                else
                    fe->pre_emphasis_prior = fe->spch[len - 1];
            }
            else
                _fe_short_to_frame(fe->spch, fe->frame, len);

            /* Zero pad up to FFT size. */
            memset(fe->frame + len, 0, (fe->fft_size - len) * sizeof(*fe->frame));

            /* Window. */
            _fe_hamming_window(fe->frame, fe->hamming_window, fe->frame_size,
                            fe->remove_dc);

            return len;
        }

        static void
        _fe_pre_emphasis(int16 const *in, frame_t * out, int32 len,
                        float32 factor, int16 prior)
        {
            int i;

        #if defined(FIXED_POINT)
            fixed32 fxd_alpha = FLOAT2FIX(factor);
            out[0] = ((fixed32) in[0] << DEFAULT_RADIX) - (prior * fxd_alpha);
            for (i = 1; i < len; ++i)
                out[i] = ((fixed32) in[i] << DEFAULT_RADIX)
                    - (fixed32) in[i - 1] * fxd_alpha;
        #else
            out[0] = (frame_t) in[0] - (frame_t) prior *factor;
            for (i = 1; i < len; i++)
                out[i] = (frame_t) in[i] - (frame_t) in[i - 1] * factor;
        #endif
        }

        static void
        _fe_short_to_frame(int16 const *in, frame_t * out, int32 len)
        {
            int i;

        #if defined(FIXED_POINT)
            for (i = 0; i < len; i++)
                out[i] = (int32) in[i] << DEFAULT_RADIX;
        #else                           /* FIXED_POINT */
            for (i = 0; i < len; i++)
                out[i] = (frame_t) in[i];
        #endif                          /* FIXED_POINT */
        }

        static void
        _fe_hamming_window(frame_t * in, window_t * window, int32 in_len,
                        int32 remove_dc)
        {
            int i;

            if (remove_dc) {
                frame_t mean = 0;

                for (i = 0; i < in_len; i++)
                    mean += in[i];
                mean /= in_len;
                for (i = 0; i < in_len; i++)
                    in[i] -= (frame_t) mean;
            }

            for (i = 0; i < in_len / 2; i++) {
                in[i] = COSMUL(in[i], window[i]);
                in[in_len - 1 - i] = COSMUL(in[in_len - 1 - i], window[i]);
            }
        }

        int
        _fe_shift_frame(fe_t * fe, int16 const *in, int32 len)
        {
            int offset, i;

            if (len > fe->frame_shift)
                len = fe->frame_shift;
            offset = fe->frame_size - fe->frame_shift;

            /* Shift data into the raw speech buffer. */
            memmove(fe->spch, fe->spch + fe->frame_shift,
                    offset * sizeof(*fe->spch));
            memcpy(fe->spch + offset, in, len * sizeof(*fe->spch));
            /* Swap and dither if necessary. */
            if (fe->swap)
                for (i = 0; i < len; ++i)
                    SWAP_INT16(&fe->spch[offset + i]);
            if (fe->dither)
                for (i = 0; i < len; ++i)
                    fe->spch[offset + i]
                        //+= (int16) ((!(s3_rand_int31() % 4)) ? 1 : 0); //race condition
                        += (int16) ((!(_genrand.genrand_int31() % 4)) ? 1 : 0); //race condition

            return _fe_spch_to_frame(fe, offset + len);
        }


        int32
        _fe_end_utt(fe_t * fe, mfcc_t * cepvector, int32 * nframes)
        {
            /* Process any remaining data, not very accurate for the VAD */
            *nframes = 0;
            if (fe->num_overflow_samps > 0) {
                _fe_read_frame(fe, fe->overflow_samps, fe->num_overflow_samps);
                fe_write_frame(fe, cepvector, FALSE);
                if (fe->vad_data->in_speech)
                    *nframes = 1;
            }

            /* reset overflow buffers... */
            fe->num_overflow_samps = 0;

            return 0;
        }

};