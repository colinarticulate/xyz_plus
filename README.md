# xyz_plus  
Articulate's version of pocketsphinx in Go as of 4th of May 2022.  

The module xyz_plus is the Go wrapper for xyzpocketsphinx_continuous and xyzpocketsphinx_batch. 

This version requires our xyz's version of pocketsphinx (https://github.com/DavidBarbera/articulate-pocketsphinx) installed in the system with logging disabled, which is currently in the main branch.

caller_plus is a module to test xyz_plus but exists in another repository (https://github.com/DavidBarbera/articulate-pocketsphinx-go/caller_plus) .

## Requirements
Go  
compiler: g++   
libraries: -lxyzsphinxbase, -lxyzsphinxad, -lxyzpocketsphinx (These libraries will be available after installing articulate-pocketsphinx in the system)    

## Versions  
### v2.0.0  
Adds error handling for runtime errors.

### v1.1.1  
Solved possible race conditions. No race conditions detected by valgrind.  

### v1.1.0  
Batch call is optimised by not loading dictionaries. This results on a ~10-fold speedup for each call.  
  
### v1.0.0  
Continuous and Batch calls are implemented. Batch is not optimised and lasts a litlle bit longer than a continuous call.  


