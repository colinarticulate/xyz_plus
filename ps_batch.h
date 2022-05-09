#include <stdexcept>
#include <limits>
#include <iostream>
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
        float* _cepstral_mean;


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

            return 0;
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
            long total, pos, endpos;

            ps_start_stream(_ps);
            ps_start_utt(_ps);

            /* If this file is seekable or maxsamps is specified, then decode
            * the whole thing at once. */
            if (maxsamps != -1) {
                data = (int16*)ckd_calloc(maxsamps, sizeof(*data));
                total = fread(data, sizeof(*data), maxsamps, rawfh);
                ps_process_raw(_ps, data, total, FALSE, TRUE);
                ckd_free(data);
            } else if ((pos = ftell(rawfh)) >= 0) {
                fseek(rawfh, 0, SEEK_END);
                endpos = ftell(rawfh);
                fseek(rawfh, pos, SEEK_SET);
                maxsamps = endpos - pos;

                data = (int16*)ckd_calloc(maxsamps, sizeof(*data));
                total = fread(data, sizeof(*data), maxsamps, rawfh);
                ps_process_raw(_ps, data, total, FALSE, TRUE);
                ckd_free(data);
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
            ps_end_utt(_ps);
             try {
                extract_cepstral_mean();
             }catch( std::exception& e){
                std::cerr << e.what() << std::endl;
            }
            return total;
        }

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
                if (ps_set_jsgf_from_buffer(_ps, PS_DEFAULT_SEARCH, path, NULL, 0)
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