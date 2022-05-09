//Inspired by:
//https://sourceforge.net/p/cmusphinx/discussion/help/thread/a445aa5c/
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
        ps_seg_t *_seg;
        int32 _score;

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
            _ps = ps_init_buffered(_config, _jsgf_buffer, _jsgf_buffer_size);
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


        int retrieve_results(){

            char buffer[256];
            buffer[0]='\0';
            _result[0]='\0';
            /* Log a backtrace if requested. */
            //if (cmd_ln_boolean_r(_config, "-backtrace")) {
                // FILE *fresult=NULL;
                // fresult=fopen("result.txt","w");
                // if (fresult==NULL){
                //     printf("Couldn't open file for results.");
                // }

                // ps_seg_t *seg;
                // int32 score;

                const char *hyp = ps_get_hyp(_ps, &_score);

                if (hyp != NULL) {
                    //E_INFO("%s (%d)\n", hyp, score);
                    sprintf(buffer, "%s*%d*", hyp, _score);
                    strcat(_result, buffer);
                    //E_INFO_NOFN("%-20s %-5s %-5s\n", "word", "start", "end");

                    // fprintf(fresult, "%s (%d)\n", hyp, score);
                    // fflush(fresult);
                    // fprintf(fresult, "%-20s %-5s %-5s\n", "word", "start", "end");
                    // fflush(fresult);


                    for ( _seg = ps_seg_iter(_ps); _seg; _seg = ps_seg_next(_seg) ) {
                        int sf, ef;
                        char const *word = ps_seg_word(_seg);
                        ps_seg_frames(_seg, &sf, &ef);
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



            //} //if (cmd_ln_boolean_r(_config, "-backtrace")) {

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
            int rv;
            ps_start_utt(_ps);
            utt_started = FALSE;
            int loop = 0;
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
                loop++;
            }
            //printf("loops: %d\n", loop);
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


            fclose(file);
            //return _result_size;


        }

};