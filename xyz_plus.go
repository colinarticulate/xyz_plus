package xyz_plus

/*
//#cgo CXXFLAGS: -g -O2 -std=c++11
#cgo CXXFLAGS: -g -Wall -Og -ggdb -std=c++11
#cgo CXXFLAGS: -Wno-unused-result -Wno-unused-but-set-variable -Wno-unused-function -Wno-unused-parameter -Wno-unused-variable
#cgo CXXFLAGS: -I${SRCDIR}/xyzsphinxbase/fe
#cgo CXXFLAGS: -I${SRCDIR}/xyzsphinxbase
#cgo CXXFLAGS: -I${SRCDIR}/aside_ps_library
#cgo CXXFLAGS: -I/usr/local/include/xyzsphinxbase
#cgo CXXFLAGS: -I/usr/local/include/xyzpocketsphinx

#cgo LDFLAGS: -lm -lpthread -pthread -lstdc++
#cgo LDFLAGS: -lxyzsphinxad -lxyzsphinxbase -lxyzpocketsphinx

#include <stdlib.h>

extern char* ps_continuous_call(void* jsgf_buffer, int jsgf_buffer_size, void* audio_buffer, int audio_buffer_size, int argc, char *argv[], char* result, int rsize);
extern char* ps_batch_call(void* audio_buffer, int audio_buffer_size, int argc, char *argv[], char* result, int rsize);
*/
import "C"

import (
	"errors"
	"fmt"
	_ "runtime/cgo"
	"strconv"
	"strings"
	"sync"
	"unsafe"
)

type _ unsafe.Pointer

// var Swig_escape_always_false bool
// var Swig_escape_val interface{}

// type _swig_fnptr *byte
// type _swig_memberptr *byte

type _ sync.Mutex

// type swig_gostring struct {
// 	p uintptr
// 	n int
// }

type Utt struct {
	Text       string
	Start, End int32
}

type UttResp struct {
	Utts []Utt
	Err  error
}

type BatchResp struct {
	Cmn []string
	Err error
}

func Ps_plus_call(arg1 []byte, arg2 []byte, arg3 []string) UttResp {

	//jsgf buffer
	bytes1 := C.CBytes(arg1)
	defer C.free(unsafe.Pointer(bytes1))
	size_bytes1 := C.int(len(arg1))

	//audio buffer
	bytes2 := C.CBytes(arg2)
	defer C.free(unsafe.Pointer(bytes2))
	size_bytes2 := C.int(len(arg2))

	cArray := C.malloc(C.size_t(len(arg3)) * C.size_t(unsafe.Sizeof(uintptr(0))))
	defer C.free(unsafe.Pointer(cArray))

	// convert the C array to a Go Array so we can index it
	a := (*[1<<30 - 1]*C.char)(cArray)
	for index, value := range arg3 {
		//a[index] = C.malloc((C.size_t(len(value)) + 1) * C.size_t(unsafe.Sizeof(uintptr(0))))
		a[index] = C.CString(value + "\000")
		//defer C.free(unsafe.Pointer(a[index]))
	}
	c_argc := C.int(len(arg3))

	//result
	_result := []string{"00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"}
	cresult := C.CString(_result[0])
	defer C.free(unsafe.Pointer(cresult))
	cresult_length := C.int(len(_result[0]))

	msg := C.ps_continuous_call(bytes1, size_bytes1, bytes2, size_bytes2, c_argc, (**C.char)(unsafe.Pointer(cArray)), cresult, cresult_length)

	if msg != nil {
		defer C.free(unsafe.Pointer(msg))
		//fmt.Println("Return error: ", C.GoString(msg))
		return UttResp{[]Utt{}, errors.New(C.GoString(msg))}
	}

	result := C.GoStringN(cresult, cresult_length)

	//Adapting result from coded string to utt struct
	if strings.Contains(result, "**") {
		raw := strings.Split(result, "**")

		if len(raw) < 2 {
			fmt.Println("xyzpocketsphinx: problems!")
			return UttResp{[]Utt{}, errors.New("Error: result from pocketsphinx continuous came back empty.")}
		}

		//fmt.Printf("%T", raw)
		fields := strings.Split(raw[0], "*")

		//fmt.Println(fields)
		// hyp := fields[0]
		// score := fields[1]

		//fmt.Println(hyp)
		//fmt.Println(strings.Split(score, ","))
		utts := []Utt{}
		//var utts = make([]Utt, len(fields)-2)

		for i := 0; i < len(fields)-2; i++ {
			parts := strings.Split(fields[2:][i], ",")
			phoneme := parts[0]
			text_start := parts[1]
			text_end := parts[2]
			start, serr := strconv.Atoi(text_start)
			end, eerr := strconv.Atoi(text_end)

			if phoneme != "(NULL)" {
				//fmt.Println(phoneme, start, end)
				//utts = append(utts, xyz_plus.Utt{phoneme, int32(start), int32(end)})
				utts = append(utts, Utt{Text: phoneme, Start: int32(start), End: int32(end)})

				if serr != nil || eerr != nil {
					fmt.Println(serr, eerr)
				}
			}
		}

		return UttResp{utts, nil}

	} else {

		return UttResp{[]Utt{}, errors.New("Error: result from pocketsphinx continuous came back corrupted.")}
	}

}

func Ps_batch_plus_call(arg2 []byte, arg3 []string) BatchResp {

	//audio buffer
	bytes2 := C.CBytes(arg2)
	defer C.free(unsafe.Pointer(bytes2))
	size_bytes2 := C.int(len(arg2))

	cArray := C.malloc(C.size_t(len(arg3)) * C.size_t(unsafe.Sizeof(uintptr(0))))
	defer C.free(unsafe.Pointer(cArray))

	// convert the C array to a Go Array so we can index it
	a := (*[1<<30 - 1]*C.char)(cArray)
	for index, value := range arg3 {
		//a[index] = C.malloc((C.size_t(len(value)) + 1) * C.size_t(unsafe.Sizeof(uintptr(0))))
		a[index] = C.CString(value + "\000")
		//defer C.free(unsafe.Pointer(a[index]))
	}
	c_argc := C.int(len(arg3))

	//result
	_result := []string{"00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"}
	cresult := C.CString(_result[0])
	defer C.free(unsafe.Pointer(cresult))
	cresult_length := C.int(len(_result[0]))

	msg := C.ps_batch_call(bytes2, size_bytes2, c_argc, (**C.char)(unsafe.Pointer(cArray)), cresult, cresult_length)

	if msg != nil {
		defer C.free(unsafe.Pointer(msg))
		//fmt.Println("Return error: ", C.GoString(msg))
		return BatchResp{[]string{}, errors.New(C.GoString(msg))}
	}

	result := C.GoStringN(cresult, cresult_length)

	//Adapting result from coded string to utt struct
	if strings.Contains(result, ",*") {
		raw := strings.Split(result, ",*")

		if len(raw) < 2 {
			fmt.Println("xyzpocketsphinx_batch: problems!")
			return BatchResp{[]string{""}, errors.New("Error: result from pocketsphinx batch came back empty.")}
		}

		numbers := strings.Split(raw[0], ",")

		return BatchResp{numbers, nil}

	} else {

		return BatchResp{[]string{""}, errors.New("Error: result from pocketsphinx batch came back corrupted.")}
	}

}
