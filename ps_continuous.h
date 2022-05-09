//Inspired by:
//https://sourceforge.net/p/cmusphinx/discussion/help/thread/a445aa5c/
//----------------------------------------------------- pocketsphinx.c
/* System headers. */
#include <stdio.h>
#include <assert.h>

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

/* SphinxBase headers. */
//#include <sphinxbase/err.h>
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

static const arg_t ps_args_def[] = {
    POCKETSPHINX_OPTIONS,
    CMDLN_EMPTY_OPTION
};

/* Feature and front-end parameters that may be in feat.params */
static const arg_t feat_defn[] = {
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

//#include <xyzsphinxbase/err.h> 
#include <xyzsphinxbase/ad.h>

#include "pocketsphinx.h"

static const arg_t cont_args_def[] = {
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



            
    public:
        char _result[512];
        int _result_size;

        //void XYZ_PocketPsphinx(void* jsgf_buffer, size_t jsgf_buffer_size, void* audio_buffer, size_t audio_buffer_size, int argc, char **argv) {
        void init(void* jsgf_buffer, size_t jsgf_buffer_size, void* audio_buffer, size_t audio_buffer_size, int argc, char **argv) {
            _jsgf_buffer = jsgf_buffer;
            _jsgf_buffer_size = jsgf_buffer_size;
            _audio_buffer = audio_buffer;
            _audio_buffer_size=audio_buffer_size;
            _argc=argc;
            //_argv=argv;

            _argv = (char**)malloc(argc * sizeof(char*));
            for(int i =0; i< argc; i++){
                if(argv[i]!=NULL) {
                    _argv[i]=(char*)malloc(sizeof(char)*strlen(argv[i])+1);
                    strcpy(_argv[i],argv[i]);
                } else {
                    printf("Error: copying parameters.");
                }
            }

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
            //     E_FATAL_SYSTEM("Failed to open file '%s' for reading",
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
            
            if (!check_wav_header(waveheader, (int)cmd_ln_float32_r(_config, "-samprate")))
                    E_FATAL("Failed to process file '%s' due to format mismatch.\n", fname);
            }

            if (strlen(fname) > 4 && strcmp(fname + strlen(fname) - 4, ".mp3") == 0) {
            E_FATAL("Can not decode mp3 files, convert input file to WAV 16kHz 16-bit mono before decoding.\n");
            }
            //---------------------------------------------------------------------------------------------------
            //int rv;
            //--------------------------------------------------------------------------------------------------------------- (loop)
            ps_start_utt(_ps);
           
            utt_started = FALSE;
            //int loop = 0;
            while ((k = fread(adbuf, sizeof(int16), 2048, file)) > 0) {
                ps_process_raw(_ps, adbuf, k, FALSE, FALSE);
                in_speech = ps_get_in_speech(_ps);
                if (in_speech && !utt_started) {
                    utt_started = TRUE;
                } 
                if (!in_speech && utt_started) {
                    ps_end_utt(_ps);
                    //hyp = ps_get_hyp(ps, NULL);
                    _result_size = retrieve_results();
                    // if (hyp != NULL)
                    // printf("%s\n", hyp);
                    
                    // if (print_times)
                    // //print_word_times();
                    // fflush(stdout);

                    ps_start_utt(_ps);
                    utt_started = FALSE;
                }
                //loop++;
            }
           // printf("loops: %d\n", loop);
            ps_end_utt(_ps);
            if (utt_started) {

                //hyp = ps_get_hyp(ps, NULL);
                _result_size = retrieve_results();
            //     if (hyp != NULL) {
            // 	    printf("%s\n", hyp);
            //         //fprintf(fresult, "%s\n", hyp);
            // 	    if (print_times) {
            // 		print_word_times();
            //     }
            // }
            }
            //fclose(fresult);
            //--------------------------------------------------------------------------------------------------------------- (loop)


            // //---- in one go:   
            // ps_decode_raw(_ps, file, _audio_buffer_size);
            // _result_size = retrieve_results();
            // //----- in one go

            fclose(file);
            //return _result_size;


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
            const char *path;
            const char *keyphrase;
            int32 lw;

            if (_config && _config != _ps->config) {
                cmd_ln_free_r(_ps->config);
                _ps->config = cmd_ln_retain(_config);
            }

            /* Set up logging. We need to do this earlier because we want to dump
            * the information to the configured log, not to the stderr. */
            if (_config && cmd_ln_str_r(_ps->config, "-logfn")) {
                if (err_set_logfile(cmd_ln_str_r(_ps->config, "-logfn")) < 0) {
                    E_ERROR("Cannot redirect log output\n");
                    return -1;
                }
            }
            
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
            if ((_ps->acmod = acmod_init(_ps->config, _ps->lmath, NULL, NULL)) == NULL)
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
            if ((_ps->d2p = dict2pid_build(_ps->acmod->mdef, _ps->dict)) == NULL)
                return -1;

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

};