// -*- C++ -*-

// Copyright 2006-2007 Deutsches Forschungszentrum fuer Kuenstliche Intelligenz
// or its licensors, as applicable.
//
// You may not use this file except under the terms of the accompanying license.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you
// may not use this file except in compliance with the License. You may
// obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// Project:
// File:
// Purpose:
// Responsible: tmb
// Reviewer:
// Primary Repository:
// Web Sites: www.iupr.org, www.dfki.de, www.ocropus.org

#define __warn_unused_result__ __far__

#include <cctype>
#include <sys/types.h>
#include <sys/stat.h>
#include <glob.h>
#include <unistd.h>
#include "colib/colib.h"
#include "iulib/iulib.h"
#include "ocropus.h"
#include "glinerec.h"
#include "bookstore.h"

namespace ocropus {
    extern param_int beam_width;
    extern param_int abort_on_error;
    extern param_string lmodel;
    extern void nustring_convert(iucstring &output,nustring &str);
    extern void nustring_convert(nustring &output,iucstring &str);

    static void store_costs(const char *base, floatarray &costs) {
        iucstring s;
        s = base;
        s.append(".costs");
        stdio stream(s,"w");
        for(int i=0;i<costs.length();i++) {
            fprintf(stream,"%d %g\n",i,costs(i));
        }
    }

    static void rseg_to_cseg(intarray &cseg, intarray &rseg, intarray &ids) {
        intarray map(max(rseg) + 1);
        map.fill(0);
        int color = 0;
        for(int i = 0; i < ids.length(); i++) {
            if(!ids[i]) continue;
            color++;
            int start = ids[i] >> 16;
            int end = ids[i] & 0xFFFF;
            if(start > end)
                throw "segmentation encoded in IDs looks seriously broken!\n";
            if(start >= map.length() || end >= map.length())
                throw "segmentation encoded in IDs doesn't fit!\n";
            for(int j = start; j <= end; j++)
                map[j] = color;
        }
        cseg.makelike(rseg);
        for(int i = 0; i < cseg.length1d(); i++)
            cseg.at1d(i) = map[rseg.at1d(i)];
    }

    static void rseg_to_cseg(const char *base, intarray &ids) {
        iucstring s;
        s = base;
        s += ".rseg.png";
        intarray rseg;
        read_image_packed(rseg, s.c_str());
        make_line_segmentation_black(rseg);
        intarray cseg;

        rseg_to_cseg(cseg, rseg, ids);

        ::make_line_segmentation_white(cseg);
        s = base;
        s += ".cseg.png";
        write_image_packed(s, cseg);
    }

    // Read a line and make an FST out of it.
    void read_transcript(IGenericFst &fst, const char *path) {
        nustring gt;
        read_utf8_line(gt, stdio(path, "r"));
        fst_line(fst, gt);
    }

    // Reads a "ground truth" FST (with extra spaces) by basename
    void read_gt(IGenericFst &fst, const char *base) {
        strbuf gt_path;
        gt_path = base;
        gt_path += ".gt.txt";

        read_transcript(fst, gt_path);
        for(int i = 0; i < fst.nStates(); i++)
            fst.addTransition(i, i, 0, 0, ' ');
    }

    int main_align(int argc,char **argv) {
        if(argc!=2) throw "usage: ... dir";
        iucstring s;
        s = argv[1];
        s += "/[0-9][0-9][0-9][0-9]/[0-9][0-9][0-9][0-9].fst";
        Glob files(s);
        for(int index=0;index<files.length();index++) {
            if(index%1000==0)
                debugf("info","%s (%d/%d)\n",files(index),index,files.length());

            iucstring base;
            base = files(index);
            base.erase(base.length()-4);

            autodel<OcroFST> gt_fst(make_OcroFST());
            read_gt(*gt_fst, base);

            autodel<OcroFST> fst(make_OcroFST());
            fst->load(files(index));
            nustring str;
            intarray v1;
            intarray v2;
            intarray in;
            intarray out;
            floatarray costs;
            try {
                beam_search(v1, v2, in, out, costs,
                            *fst, *gt_fst, beam_width);
                // recolor rseg to cseg
            } catch(const char *error) {
                fprintf(stderr,"ERROR in bestpath: %s\n",error);
                if(abort_on_error) abort();
            }
            try {
                rseg_to_cseg(base, in);
                store_costs(base, costs);
                debugf("dcost","--------------------------------\n");
                for(int i=0;i<out.length();i++) {
                    debugf("dcost","%3d %10g %c\n",i,costs(i),out(i));
                }
            } catch(const char *err) {
                fprintf(stderr,"ERROR in cseg reconstruction: %s\n",err);
                if(abort_on_error) abort();
            }
        }
        return 0;
    }


    int main_fsts2text(int argc,char **argv) {
        if(argc!=2) throw "usage: lmodel=... ocropus fsts2text dir";
        autodel<OcroFST> langmod(make_OcroFST());
        try {
            langmod->load(lmodel);
        } catch(const char *s) {
            throwf("%s: failed to load (%s)",(const char*)lmodel,s);
        } catch(...) {
            throwf("%s: failed to load language model",(const char*)lmodel);
        }
        iucstring s;
        sprintf(s,"%s/[0-9][0-9][0-9][0-9]/[0-9][0-9][0-9][0-9].fst",argv[1]);
        Glob files(s);
#pragma omp parallel for schedule(dynamic,20)
        for(int index=0;index<files.length();index++) {
            if(index%1000==0)
                debugf("info","%s (%d/%d)\n",files(index),index,files.length());
            autodel<OcroFST> fst(make_OcroFST());
            fst->load(files(index));
            nustring str;
            try {
                intarray v1;
                intarray v2;
                intarray in;
                intarray out;
                floatarray costs;
                beam_search(v1, v2, in, out, costs,
                            *fst, *langmod, beam_width);
                double cost = sum(costs);
                remove_epsilons(str, out);
                if(cost < 1e10) {
                    iucstring output;
                    nustring_convert(output,str);
                    debugf("transcript","%s\t%s\n",files(index), output.c_str());
                    iucstring base;
                    base = files(index);
                    base.erase(base.length()-4);
                    try {
                        rseg_to_cseg(base, in);
                        store_costs(base, costs);
                    } catch(const char *err) {
                        fprintf(stderr,"ERROR in cseg reconstruction: %s\n",err);
                        if(abort_on_error) abort();
                    }
                    base += ".txt";
                    fprintf(stdio(base,"w"),"%s\n",output.c_str());
                } else {
                    debugf("info","%s\t%f\n",files(index), cost);
                }
            } catch(const char *error) {
                fprintf(stderr,"ERROR in bestpath: %s\n",error);
                if(abort_on_error) abort();
            }
        }

        return 0;
    }

    int main_fsts2bestpaths(int argc,char **argv) {
        if(argc!=2) throw "usage: ... dir";
        iucstring s;
        sprintf(s,"%s/[0-9][0-9][0-9][0-9]/[0-9][0-9][0-9][0-9].fst",argv[1]);
        Glob files(s);
        for(int index=0;index<files.length();index++) {
            if(index%1000==0)
                debugf("info","%s (%d/%d)\n",files(index),index,files.length());
            autodel<IGenericFst> fst(make_OcroFST());
            fst->load(files(index));
            nustring str;
            try {
                fst->bestpath(str);
                iucstring output = str;
                debugf("transcript","%s\t%s\n",files(index),output.c_str());
                iucstring base = files(index);
                base.erase(base.length()-4);
                base += ".txt";
                fprintf(stdio(base,"w"),"%s",output.c_str());
            } catch(const char *error) {
                fprintf(stderr,"ERROR in bestpath: %s\n",error);
                if(abort_on_error) abort();
            }
        }
        return 0;
    }
}