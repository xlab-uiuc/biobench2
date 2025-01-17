#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <stdexcept>
#include <vector>
#include <list>
#include <algorithm>
#include <math.h>

#include <assert.h>
#include <time.h>
#include <pthread.h>

#include "string_utils.h"
#include "seq_contig.h"
#include "params.h"

using namespace std;

double normal_weight(double x, double sigma){
  double pi=3.14;
  return exp(-x*x/(2*sigma*sigma))/(sqrt(2.0*pi)*sigma);
}


struct pthread_pars{

  vector<int>* pos_hits;
  vector<int>* neg_hits;
  float* score_for_ptr;
  float* score_rev_ptr;

  int score_size;

  //double peak_shift;
  double kde_bandwidth;

  int calc_start_value;
  int calc_end_value;

  int thread_count;
  unsigned int calc_window;
};

int int_max(int i, int j){
  if ( i>j ) return i;
  return j;
}
int int_min(int i, int j){
  if (i<j) return i;
  return j;
}

void* pscore(void* vptr){
  pthread_pars* pars_ptr = (pthread_pars*)(vptr);

  if ( !( pars_ptr->calc_start_value <=  pars_ptr->calc_end_value )){
    cerr<<"thread: "<<pars_ptr->thread_count<<endl;
    cerr<<"start_count: "<<pars_ptr->calc_start_value<<endl;
    cerr<<"end_count: "<<pars_ptr->calc_end_value<<endl;
    assert(false);
  }

  assert( pars_ptr->calc_start_value >= 0);
  int calc_window = pars_ptr->calc_window;
  vector<int>* pos_hits = pars_ptr->pos_hits;
  vector<int>* neg_hits = pars_ptr->neg_hits;

  float* score_for = pars_ptr->score_for_ptr;
  float* score_rev = pars_ptr->score_rev_ptr;

  int score_size = pars_ptr->score_size;
  int calc_start_value = pars_ptr->calc_start_value;
  int calc_end_value = pars_ptr->calc_end_value;
  
  //  double peak_shift = pars_ptr->peak_shift;
  double kde_bandwidth = pars_ptr->kde_bandwidth;

  //locating boundaries in the hits list
  bool pos_start_found = false;
  bool pos_end_found = false;
  bool pos_end_exists = false;

  int pos_start = -1;
  int pos_end = -1;

  bool neg_start_found = false;
  bool neg_end_found = false;
  bool neg_end_exists = false;

  int neg_start = -1;
  int neg_end = -1;

  // looking for the window of pos hits corresponding to this region 
  for(int i=(int)pos_hits->size()-1; i>=0; i--){ //can replace this with the binary search
    if((*pos_hits).at(i) >= calc_start_value - calc_window){
      pos_start_found = true;
      pos_end_exists = true;
      pos_start = i;
    }
    if((*pos_hits).at(i) >= calc_end_value + calc_window){
      pos_end_found = true;
      pos_end = i;
    }
  }

  if(pos_end_found == false && pos_end_exists == true){
    if(pos_hits->size() > 0){
      pos_end_found = true;
      pos_end = pos_hits->size();
    }
  }

  // looking for the window of neg hits corresponding to this region 
  for(int i = (int)neg_hits->size()-1; i>=0; i--){ //can replace this with the binary search
    if((*neg_hits).at(i) >= calc_start_value - calc_window){
      neg_start_found = true;
      neg_end_exists = true;
      neg_start = i;
    }
    if((*neg_hits).at(i) >= calc_end_value + calc_window){
      neg_end_found = true;
      neg_end = i;
    }
  }
  
  if(neg_end_found == false && neg_end_exists == true){
    if(neg_hits->size() > 0){
      neg_end_found = true;
      neg_end = neg_hits->size();
    }
  }

  
  if(pos_start_found && pos_end_found){
    for(int i=pos_start; i<pos_end; i++){
      int cur_pos_coord = (*pos_hits).at(i);
      for(int j=int_max(0, cur_pos_coord - calc_window/2 /*+ (int)peak_shift*/); 
	  j<=int_min(score_size-1, cur_pos_coord + calc_window/2 /*+ (int)peak_shift*/); j++){
	if(j<0 || j>score_size){
	  cerr<<"out of boundary access in pscore"<<endl;
	  exit(1);
	}

	score_for[j] += normal_weight(j-cur_pos_coord/*-peak_shift*/, kde_bandwidth);
      }
    }
  }

  if(neg_start_found && neg_end_found){
    for(int i=neg_start; i<neg_end; i++){
      int cur_pos_coord = (*neg_hits).at(i);
      for(int j=int_max(0, cur_pos_coord - calc_window/2 /*- (int)peak_shift*/); 
	  j<=int_min(score_size-1, cur_pos_coord + calc_window/2 /*- (int)peak_shift*/); j++){

	if(j<0 || j>score_size){
	  cerr<<"out of boundary access in pscore"<<endl;
	  exit(1);
	}
	score_rev[j] += normal_weight(j-cur_pos_coord/*+peak_shift*/, kde_bandwidth);
      }
    }
  }

  return 0;
}


int main(int argc, char* argv[]){
  
  params pars(argc, argv);
  pars.require("align_file","align_file",STRING_TYPE);
  pars.require("contig_id","contig_id",STRING_TYPE);
  pars.require("contig_size","contig_size",INT_TYPE);
  pars.require("output_score_file","output_score_file",STRING_TYPE);

  pars.require("region_start", "region_start", STRING_TYPE);
  pars.require("region_end", "region_end", STRING_TYPE);

  //  pars.optional("peak_shift","peak_shift","50",INT_TYPE);
  pars.optional("calc_window","calc_window","300",INT_TYPE);
  pars.optional("kde_bandwidth","kde_bandwidth","30",DOUBLE_TYPE);  
  pars.optional("threads","threads","7",INT_TYPE);
  
  if(!pars.enforce()){
    exit(1);
  }

  cout<<endl;
  pars.list_all_params();

  string align_fname = pars.get_string_value("align_file");
  string contig_id = pars.get_string_value("contig_id");
  int contig_size = pars.get_int_value("contig_size");

  int region_start = pars.get_int_value("region_start");
  int region_end = pars.get_int_value("region_end");

  if(contig_size <= 0){
    cout<<"Error: wrong contig size: "<<contig_size<<endl;
    exit(0);
  }

  string output_fname = pars.get_string_value("output_score_file");

  //int peak_shift = pars.get_int_value("peak_shift");
  int calc_window = pars.get_int_value("calc_window");
  double kde_bandwidth = pars.get_double_value("kde_bandwidth");
  int thread_num = pars.get_int_value("threads");

  cout<<endl<<"Will be using the following:"<<endl<<endl;
  //cout<<"peak_shift: "<<peak_shift<<endl;
  cout<<"kde_bandwidth: "<<kde_bandwidth<<endl;
  cout<<"calc_window: "<<calc_window<<endl;

  cout<<"align_file: "<<align_fname<<endl;

  cout<<endl;

  ifstream hit_str(align_fname.c_str());
  if(!hit_str.good()){
    cerr<<"bad file name: "<<align_fname<<endl;
    exit(1);
  }
  
  cout<<"processing contig: "<<contig_id<<" [ "<<contig_size<<" bps ]"<<endl;
  

  remove(output_fname.c_str());
  ofstream out_str(output_fname.c_str());
  if(!out_str.good()){
    cerr<<"bad file name: "<<output_fname<<endl;
    exit(1);
  }


  cout<<"reading align_file..."<<endl;
  
  int entries_pos = 0;
  int entries_neg = 0;
  string dummy_string;

  char sep1 = ' ';

  while(hit_str.good()){
    getline(hit_str, dummy_string);
    if(hit_str.good()){
      if(dummy_string.length() > 0){
	if( dummy_string[0] != '#'){    
	  
	  vector<string> cur_hit_entry_fields = split(dummy_string, sep1);
	  if(cur_hit_entry_fields.size() == 3){
	    string cur_hit_contig_name = cur_hit_entry_fields[0];
	    int cur_hit_5p_coord = atoi(cur_hit_entry_fields[1].c_str());
	    string cur_hit_orient = cur_hit_entry_fields[2];

	    if(cur_hit_orient != "+" && cur_hit_orient != "-"){
	      cout<<"Warning: expected +/- for orientation of read but found "<<cur_hit_orient;
	      cout<<". Skipping."<<endl;
	    }
	    else{
	      
	      if(cur_hit_contig_name == contig_id){
		if(cur_hit_5p_coord < 0 || cur_hit_5p_coord >= contig_size){
		  cout<<"Warning read coordinate "<<cur_hit_5p_coord<<" is out of boundaries [ 0,";
		  cout<<contig_size<<" ]. Skipping."<<endl;		
		}
		else{
	  	  		  
		  if(cur_hit_orient == "+"){
		    entries_pos++;
		  }
		  else{ 
		    entries_neg++;
		  }
		}
	      }
	    }
	  }
	}
      }   
    }
  }
  
  hit_str.close();  

  cout<<"read: "<<entries_pos<<" + entries "<<endl;
  cout<<"read: "<<entries_neg<<" - entries "<<endl;
  
  vector<int> pos_hits(entries_pos);
  vector<int> neg_hits(entries_neg);
  
  ifstream hit_str_pass2(align_fname.c_str());
  if(!hit_str_pass2.good()){
    cerr<<"Failed to read the file "<<align_fname<<endl;
    exit(1);
  }


  int entries_pos_pass2 = 0;
  int entries_neg_pass2 = 0;
  
  while(hit_str_pass2.good()){
    getline(hit_str_pass2, dummy_string);
    if(hit_str_pass2.good()){
      if(dummy_string.length() > 0){
	if( dummy_string[0] != '#'){    
	  
	  vector<string> cur_hit_entry_fields = split(dummy_string, sep1);
	  if(cur_hit_entry_fields.size() == 3){
	    string cur_hit_contig_name = cur_hit_entry_fields[0];
	    int cur_hit_5p_coord = atoi(cur_hit_entry_fields[1].c_str());
	    string cur_hit_orient = cur_hit_entry_fields[2];
	    
	    if(cur_hit_orient != "+" && cur_hit_orient != "-"){
	      cout<<"Warning: expected +/- for orientation of read but found "<<cur_hit_orient;
	      cout<<". Skipping."<<endl;
	    }
	    else{	      
	      if(cur_hit_contig_name == contig_id){
		if(cur_hit_5p_coord < 0 || cur_hit_5p_coord >= contig_size){
		  cout<<"Warning read coordinate "<<cur_hit_5p_coord<<" is out of boundaries [ 0,";
		  cout<<contig_size<<" ]. Skipping."<<endl;		
		}
		else{
		  int start_coord = cur_hit_5p_coord;
	  	  		  
		  if(cur_hit_orient == "+"){
		    if(entries_pos_pass2 >= entries_pos){
		      cout<<"reading out of bounds. Aborting."<<endl;
		      exit(1);
		    }
		    pos_hits[entries_pos_pass2] = start_coord;
		    entries_pos_pass2++;
		  }
		  else{ 
		    if(entries_neg_pass2 >= entries_neg){
		      cout<<"reading out of bounds. Aborting."<<endl;
		      exit(1);
		    }
		    neg_hits[entries_neg_pass2] = start_coord;
		    entries_neg_pass2++;
		  }
		}
	      }
	    }
	  }
	}
      }   
    }
  }
  
  hit_str_pass2.close();  

  //cout<<"read: "<<entries_pos_pass2<<" positive reads"<<endl;
  //cout<<"read: "<<entries_neg_pass2<<" negative reads"<<endl;

  cout<<"sorting hits"<<endl;
  sort(pos_hits.begin(), pos_hits.end());
  sort(neg_hits.begin(), neg_hits.end());

  cout<<"allocating memory for density profile CDP"<<endl;
  
  float *scores_for = new float[contig_size];
  float *scores_rev = new float[contig_size];
  
  cout<<"zeroing the score array"<<endl;
  for(int i=0; i<contig_size; i++){
    scores_for[i] = 0;
    scores_rev[i] = 0;
  }

  cout<<"calculating scores"<<endl;

  cout<<"using "<<thread_num<<" parallel threads"<<endl;

  pthread_t* threads = new pthread_t[thread_num];
  pthread_pars* pars_t = new pthread_pars[thread_num];

  int set_size = ( ( contig_size - calc_window ) - ( calc_window ) );
  int remainder = set_size % thread_num;
  int portion = (set_size - remainder) / thread_num;
  
  int last_end = calc_window;

  for(int i=0; i<thread_num; i++){
    
    int cur_start_value = last_end;
    int cur_end_value;
    
    if( i < remainder ) cur_end_value = cur_start_value + portion + 1;
    else cur_end_value = cur_start_value + portion;

    assert( cur_end_value > cur_start_value);
    assert( cur_start_value >= 0);

    last_end = cur_end_value;

    pars_t[i].pos_hits = &pos_hits;
    pars_t[i].neg_hits = &neg_hits;
   
    pars_t[i].score_for_ptr = scores_for;
    pars_t[i].score_rev_ptr = scores_rev;
    pars_t[i].score_size = contig_size;

    pars_t[i].kde_bandwidth = kde_bandwidth;
    //pars_t[i].peak_shift = peak_shift;    
    pars_t[i].calc_start_value = cur_start_value;
    pars_t[i].calc_end_value = cur_end_value;
    pars_t[i].thread_count = i;
    pars_t[i].calc_window = calc_window;

    if( i == thread_num - 1){
      if( cur_end_value != (int)contig_size - calc_window ){
	cerr<<"failed to match the end. expected: "<<contig_size - calc_window;
	cerr<<" but obtained: "<<cur_end_value<<endl;
	assert(false);
      }      
    }
  }

  for(int i=0; i<thread_num; i++){
    pthread_create( threads+i, NULL, pscore, (void*)(pars_t+i) );
  }

  for(int i=0; i<thread_num; i++){
    pthread_join( *(threads+i), NULL);
  }
  

  cout<<"saving the score file"<<endl;

  //int entries = entries_pos + entries_neg;

  /*
  cout<<"/some debug info start"<<endl;
  cout<<"sizeof(contig_size):  "<<sizeof(contig_size)<<endl;
  cout<<"sizeof(entries): "<<sizeof(entries)<<endl;
  cout<<"sizeof(scores[0])*contig_size: "<<sizeof(scores[0])*contig_size<<endl;
  cout<<"/some debug info end"<<endl;
  */


  /*
  out_str.write((char*)(&contig_size), sizeof(contig_size));
  out_str.write((char*)(&entries), sizeof(entries));
  out_str.write((char*)(&scores[0]), sizeof(scores[0])*contig_size);
  */

  for(int i=region_start; i<=region_end; i++){
    out_str<<i<<"\t"<<scores_for[i]<<"\t"<<scores_rev[i]<<endl;
  }
  
  
  out_str.close();

  cout<<"saved ok"<<endl;

  delete[] threads;
  delete[] pars_t;
  delete[] scores_for;
  delete[] scores_rev;
  
  return 0;

}
