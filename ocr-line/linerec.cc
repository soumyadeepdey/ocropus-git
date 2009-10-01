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
#include <unistd.h>
#include "ocropus.h"
#include "glinerec.h"
#include "glutils.h"

using namespace iulib;
using namespace colib;
using namespace ocropus;
using namespace narray_ops;
using namespace narray_io;
using namespace glinerec;

namespace {
    Logger logger("glr");

    void push_unary(floatarray &v,float value,float lo,float hi,
                    int steps,float weight=1.0) {
        float delta = (hi-lo)/steps;
        for(int i=0;i<steps;i++) {
            float thresh = i * delta + lo;
            if(value>=thresh) v.push(weight);
            else v.push(0.0);
        }
    }
}

namespace glinerec {

    void centroid(float &x,float &y,bytearray &image) {
        float total = 0;
        for(int i=0;i<image.dim(0);i++) {
            for(int j=0;j<image.dim(1);j++) {
                float value = image(i,j);
                total += value;
                x += i*value;
                y += j*value;
            }
        }
        x /= total;
        y /= total;
    }

    // boundary feature extractor

    void check_sorted(intarray &a) {
        for(int i=1;i<a.length();i++)
            CHECK_ARG(a(i)>=a(i-1));
    }

    bool equals(intarray &a,intarray &b) {
        if(a.length()!=b.length()) return 0;
        for(int i=0;i<a.length();i++)
            if(a[i]!=b[i]) return 0;
        return 1;
    }


    void segmentation_correspondences(objlist<intarray> &segments,intarray &seg,intarray &cseg) {
        CHECK_ARG(max(seg)<10000);
        CHECK_ARG(max(cseg)<10000);
        int nseg = max(seg)+1;
        int ncseg = max(cseg)+1;
        intarray overlaps(nseg,ncseg);
        overlaps = 0;
        CHECK_ARG(seg.length()==cseg.length());
        for(int i=0;i<seg.length();i++)
            overlaps(seg[i],cseg[i])++;
        segments.clear();
        segments.resize(ncseg);
        for(int i=0;i<nseg;i++) {
            int j = rowargmax(overlaps,i);
            ASSERT(j>=0 && j<ncseg);
            segments(j).push(i);
        }
    }


    struct CenterFeatureMap : IFeatureMap {
        bytearray image;
        autodel<IFeatureMap> fmap;
        CenterFeatureMap() {
            make_component(fmap,"sfmap");
            pdef("csize",40,"target character size after rescaling");
            pdef("maxheight",300,"maximum height of input line");
            pdef("context",1.5,"how much context to include (1.0=no context)");
            pdef("mdilate",2,"dilate the extraction mask by this much");
            pdef("minsize_factor",1.3,"minimum size of bounding box in terms of xheight");
            pdef("use_props",1,"use character properties (aspect ratio, etc.)");
        }
        const char *name() {
            return "cfmap";
        }
        const char *interface() {
            return "IFeatureMap";
        }

        float intercept,slope,xheight,descender_sink,ascender_rise;

        void setLine(bytearray &image) {
            this->image = image;
            fmap->setLine(image);
            // FIXME get rid of this call
            get_extended_line_info_using_ccs(intercept,slope,xheight,
                                             descender_sink,ascender_rise,image);
            if(xheight<4) throw BadTextLine();
            show_baseline(slope,intercept,xheight,image,"YYY");
            bytearray baseline_image;
            debug_baseline(baseline_image,slope,intercept,xheight,image);
            logger.log("baseline\n",baseline_image);
            debugf("detail","LineInfo %g %g %g %g %g\n",intercept,slope,xheight,descender_sink,ascender_rise);
        }

        void pushProps(InputVector &iv,rectangle b) {
            float baseline = intercept + slope * b.x0;
            float bottom = (b.y0-baseline)/xheight;
            float top = (b.y1-baseline)/xheight;
            float width = b.width() / float(xheight);
            float height = b.height() / float(xheight);
            float aspect = log(b.height() / float(b.width()));
            int csize = pgetf("csize");
            floatarray v;
            push_unary(v,top,-1,4,csize);
            push_unary(v,bottom,-1,4,csize);
            push_unary(v,width,-1,4,csize);
            push_unary(v,height,-1,4,csize);
            push_unary(v,aspect,-1,4,csize);
            v.resize(v.length()/csize,csize);
            iv.append(v,"props");
        }

        void extractFeatures(InputVector &v,rectangle b_,bytearray &mask) {
            rectangle b;
            b = b_;
            dsection("featcenter");
            float context = pgetf("context");
            int mdilate = pgetf("mdilate");
            CHECK_ARG(b.height()<pgetf("maxheight"));
            if(mdilate>0) {
                pad_by(mask,mdilate,mdilate);
                b.pad_by(mdilate,mdilate);
                binary_dilate_circle(mask,mdilate);
            }
            if(b.width()>b.height()) {
                int dy = (b.width()-b.height())/2;
                b.y0 -= dy;
                b.y1 += dy;
                pad_by(mask,0,dy);
            } else {
                int dx = (b.height()-b.width())/2;
                b.x0 -= dx;
                b.x1 += dx;
                pad_by(mask,dx,0);
            }
            int r = int((context-1.0)*b.width());
            if(r>0) {
                pad_by(mask,r,r);
                b.x0 -= r;
                b.y0 -= r;
                b.x1 += r;
                b.y1 += r;
            }

            float minsize_factor = pgetf("minsize_factor");
            if(minsize_factor>=0.0) {
                int minsize = int(minsize_factor*xheight);

                int p = max(minsize-b.width(),minsize-b.height());
                debugf("minsize","w=%d h=%d xh=%d f=%g p=%d\n",
                       b.width(),b.height(),int(xheight),minsize_factor,p);
                if(p>0) {
                    pad_by(mask,p,p);
                    b.x0 -= p;
                    b.y0 -= p;
                    b.x1 += p;
                    b.y1 += p;
                }
            }

            if(dactive()) {
                floatarray temp,mtemp;
                temp.resize(b.width(),b.height()) = 0;
                mtemp.resize(b.width(),b.height()) = 0;
                for(int i=0;i<temp.dim(0);i++)
                    for(int j=0;j<temp.dim(1);j++)
                        temp(i,j) = bat(image,i+b.x0,j+b.y0,0);
                temp /= max(temp);
                dshown(temp,"a");
                dshown(mask,"b");
                mtemp = mask;
                mtemp /= max(mtemp);
                mtemp *= 0.5;
                temp -= mtemp;
                dshown(temp,"c");
                dwait();
            }
            if(b.height()>=pgetf("maxheight")) {
                throwf("feature extraction: bbox height %d > maxheight %g",
                       b.height(),pgetf("maxheight"));
            }
            fmap->extractFeatures(v,b,mask);
            if(pgetf("use_props")) pushProps(v,b_);
        }
    };

    struct LinerecExtracted : IRecognizeLine {
        enum { reject_class = '~' };
        autodel<ISegmentLine> segmenter;
        autodel<IGrouper> grouper;
        autodel<IModel> classifier;
        autodel<IFeatureMap> featuremap;
        intarray counts;
        bool counts_warned;
        int ntrained;

        LinerecExtracted() {
            // component choices
            pdef("classifier","latin","character classifier");
            pdef("fmap","cfmap","feature map to be used for recognition");
            // retraining
            pdef("cpreload","none","classifier to be loaded prior to training");
            // debugging
            pdef("verbose",0,"verbose output from glinerec");
            // outputs
            pdef("use_priors",0,"correct the classifier output by priors");
            pdef("use_reject",1,"use a reject class (use posteriors only and train on junk chars)");
            pdef("maxcost",20.0,"maximum cost of a character to be added to the output");
            pdef("minclass",32,"minimum output class to be added (default=unicode space)");
            pdef("minprob",1e-6,"minimum probability for a character to appear in the output at all");
            // segmentation
            pdef("maxrange",5,"maximum number of components that are grouped together");
            // sanity limits on input
            pdef("maxheight",300,"maximum height of input line");
            pdef("maxaspect",1.0,"maximum height/width ratio of input line");
            // space estimation (FIXME factor this out eventually)
            pdef("space_fractile",0.5,"fractile for space estimation");
            pdef("space_multiplier",2,"multipler for space estimation");
            pdef("space_min",0.2,"minimum space threshold (in xheight)");
            pdef("space_max",1.1,"maximum space threshold (in xheight)");
            pdef("space_yes",1.0,"cost of inserting a space");
            pdef("space_no",5.0,"cost of not inserting a space");

            persist(featuremap,"featuremap");
            persist(classifier,"classifier");
            persist(counts,"counts");

            segmenter = make_DpSegmenter();
            grouper = make_SimpleGrouper();
            featuremap = dynamic_cast<IFeatureMap*>(component_construct(pget("fmap")));
            classifier = make_model(pget("classifier"));
            if(!classifier) throw "construct_model didn't yield an IModel";
            ntrained = 0;
            counts_warned = 0;
        }

        const char *name() {
            return "linerec";
        }

        void inc_class(int c) {
            while(counts.length()<=c)
                counts.push(0);
            counts(c)++;
        }

        void info(int depth,FILE *stream) {
            iprintf(stream,depth,"LinerecExtracted\n");
            pprint(stream,depth);
            iprintf(stream,depth,"segmenter: %s\n",!segmenter?"null":segmenter->description());
            iprintf(stream,depth,"grouper: %s\n",!grouper?"null":grouper->description());
            iprintf(stream,depth,"counts: %d %d\n",counts.length(),(int)sum(counts));
            featuremap->info(depth,stream);
            classifier->info(depth,stream);
        }

        const char *description() {
            return "Linerec";
        }
        const char *command(const char *argv[]) {
            return classifier->command(argv);
        }

#if 0
        void save(FILE *stream) {
            // NB: to be backwards compatible, all magic strings have
            // the same size
            magic_write(stream,"linerc2");
            psave(stream);
            // FIXME save grouper and segmenter here
            save_component(stream,featuremap.ptr());
            save_component(stream,classifier.ptr());
            narray_write(stream,counts);
        }
        void load(FILE *stream) {
            strg magic;
            // NB: to be backwards compatible, all magic strings must
            // have the same size
            magic_get(stream,magic,strlen("linerec"));
            CHECK(magic=="linerec" || magic=="linerc2");
            pload(stream);
            featuremap = dynamic_cast<IFeatureMap*>(load_component(stream));
            classifier = dynamic_cast<IModel*>(load_component(stream));

            counts.clear();
            if(magic=="linerec") {
                pset("minsize_factor",0.0);
            } else if(magic=="linerc2") {
                narray_read(stream,counts);
            }

            // FIXME load grouper and segmenter here

            // now reload the environment variables
            reimport();
        }
#endif

        void startTraining(const char *) {
            const char *preload = pget("cpreload");
            if(strcmp(preload,"none")) {
                stdio stream(preload,"r");
                load(stream);
                debugf("info","preloaded classifier %s\n");
                classifier->info();
            }
        }

        void finishTraining() {
            classifier->updateModel();
        }

        ustrg transcript;
        bytearray line;
        intarray segmentation;

        bytearray binarized;
        void setLine(bytearray &image) {
            CHECK_ARG(image.dim(1)<pgetf("maxheight"));
            // initialize the feature map to the line image
            featuremap->setLine(image);

            // run the segmenter
            binarize_simple(binarized,image);
            segmenter->charseg(segmentation,binarized);
            sub(255,binarized);
            make_line_segmentation_black(segmentation);
            renumber_labels(segmentation,1);

            // set up the grouper
            grouper->setSegmentation(segmentation);

            // debugging info
            IDpSegmenter *dp = dynamic_cast<IDpSegmenter*>(segmenter.ptr());
            if(dp) {
                logger.log("DpSegmenter",dp->dimage);
                dshow(dp->dimage,"YYy");
            }
            logger.recolor("segmentation",segmentation);
            dshowr(segmentation,"YYY");
        }

        int riuniform(int hi) {
            return lrand48()%hi;
        }

        void addTrainingLine(intarray &cseg,ustrg &tr) {
            bytearray gimage;
            segmentation_as_bitmap(gimage,cseg);
            addTrainingLine(cseg,gimage,tr);
        }

        void addTrainingLine(intarray &cseg,bytearray &image,ustrg &tr) {
            if(image.dim(1)>pgetf("maxheight"))
                throwf("input line too high (%d x %d)",image.dim(0),image.dim(1));
            if(image.dim(1)*1.0/image.dim(0)>pgetf("maxaspect"))
                throwf("input line has bad aspect ratio (%d x %d)",image.dim(0),image.dim(1));
            bool use_reject = pgetf("use_reject");
            dsection("training");
            CHECK(image.dim(0)==cseg.dim(0) && image.dim(1)==cseg.dim(1));
            current_recognizer_ = this;

            // check and set the transcript
            transcript = tr;
            setLine(image);
            for(int i=0;i<transcript.length();i++)
                CHECK_ARG(transcript(i).ord()>=32);

            // compute correspondences between actual segmentation and
            // ground truth segmentation
            objlist<intarray> segments;
            segmentation_correspondences(segments,segmentation,cseg);
            dshowr(segmentation,"yy");
            dshowr(cseg,"yY");

            // now iterate through all the hypothesis segments and
            // train the classifier with them
            int total = 0;
            int junk = 0;
//#pragma omp parallel for
            for(int i=0;i<grouper->length();i++) {
                intarray segs;
                grouper->getSegments(segs,i);

                // see whether this is a ground truth segment
                int match = -1;
                for(int j=0;j<segments.length();j++) {
                    if(equals(segments(j),segs)) {
                        match = j;
                        break;
                    }
                }
                match -= 1;         // segments are numbered starting at 1
                int c = reject_class;
                if(match>=0) {
                    if(match>=transcript.length()) {
                        utf8strg utf8Transcript;
                        transcript.utf8EncodeTerm(utf8Transcript);
                        debugf("info","mismatch between transcript and cseg: \"%s\"\n",utf8Transcript.c_str());
                        continue;
                    } else {
                        c = transcript[match].ord();
                        debugf("debugmismatch","index %d position %d char %c [%d]\n",i,match,c,c);
                    }
                }

                if(c==reject_class) junk++;

                // extract the character and add it to the classifier
                rectangle b;
                bytearray mask;
                grouper->getMask(b,mask,i,0);
                InputVector v;
                featuremap->extractFeatures(v,b,mask);
#pragma omp atomic
                total++;
#pragma omp critical
                {
                    if(use_reject) {
                        classifier->add(v.ravel(),c);
                    } else {
                        if(c!=reject_class)
                            classifier->add(v.ravel(),c);
                    }
                    if(c!=reject_class) inc_class(c);
                }
#pragma omp atomic
                ntrained++;
            }
            debugf("detail","addTrainingLine trained %d chars, %d junk, %s total\n",total-junk,junk,
                    classifier->command("total"));
            dwait();
        }

        enum { high_cost = 100 };

        void recognizeLine(IGenericFst &result,bytearray &image) {
            intarray segmentation_;
            recognizeLine(segmentation_,result,image);
        }

        float space_threshold;

        void estimateSpaceSize() {
            intarray labels;
            labels = segmentation;
            label_components(labels);
            rectarray boxes;
            bounding_boxes(boxes,labels);
            floatarray distances;
            distances.resize(boxes.length()) = 99999;
            for(int i=1;i<boxes.length();i++) {
                rectangle b = boxes[i];
                for(int j=1;j<boxes.length();j++) {
                    rectangle n = boxes[j];
                    int delta = n.x0 - b.x1;
                    if(delta<0) continue;
                    if(delta>=distances[i]) continue;
                    distances[i] = delta;
                }
            }
            float interchar = fractile(distances,pgetf("space_fractile"));
            space_threshold = interchar*pgetf("space_multiplier");;
            // impose some reasonable upper and lower bounds
            float xheight = 10.0; // FIXME
            space_threshold = max(space_threshold,pgetf("space_min")*xheight);
            space_threshold = min(space_threshold,pgetf("space_max")*xheight);
        }

        void recognizeLine(intarray &segmentation_,IGenericFst &result,bytearray &image_) {
            if(image_.dim(1)>pgetf("maxheight"))
                throwf("input line too high (%d x %d)",image_.dim(0),image_.dim(1));
            if(image_.dim(1)*1.0/image_.dim(0)>pgetf("maxaspect"))
                throwf("input line has bad aspect ratio (%d x %d)",image_.dim(0),image_.dim(1));
            bool use_reject = pgetf("use_reject");
            bytearray image;
            image = image_;
            dsection("recognizing");
            logger.log("input\n",image);
            setLine(image);
            segmentation_ = segmentation;
            bytearray available;
            floatarray cp,ccosts,props;
            OutputVector p;
            int ncomponents = grouper->length();
            int minclass = pgetf("minclass");
            float minprob = pgetf("minprob");
            float space_yes = pgetf("space_yes");
            float space_no = pgetf("space_no");
            float maxcost = pgetf("maxcost");

            // compute priors if possible; fall back on
            // using no priors if no counts are available
            floatarray priors;
            bool use_priors = pgetf("use_priors");
            if(use_priors) {
                if(counts.length()>0) {
                    priors = counts;
                    priors /= sum(priors);
                } else {
                    if(!counts_warned)
                        debugf("warn","use_priors specified but priors unavailable (old model)\n");
                    use_priors = 0;
                    counts_warned = 1;
                }
            }

            estimateSpaceSize();

#pragma omp parallel for schedule(dynamic,10) private(p,props)
            for(int i=0;i<ncomponents;i++) {
                rectangle b;
                bytearray mask;
                grouper->getMask(b,mask,i,0);
                InputVector v;
                try {
                    featuremap->extractFeatures(v,b,mask);
                } catch(const char *msg) {
                    debugf("warn","feature extraction failed [%d]: %s\n",i,msg);
                    continue;
                }
                float ccost = 0.0;
                {
                    InputVector temp(v);
                    ccost = classifier->outputs(p,temp);
                }
#pragma omp critical
                {
                    if(use_reject) {
                        ccost = 0;
                        float total = sum(p.values);
                        if(total>1e-11)
                            p.values /= total;
                        else
                            p.values = 0.0;
                    }
                    int count = 0;
#if 0
                    for(int j=minclass;j<p.length();j++) {
                        if(j==reject_class) continue;
                        if(p(j)<minprob) continue;
                        float pcost = -log(p(j));
                        debugf("dcost","%3d %10g %c\n",j,pcost+ccost,(j>32?j:'_'));
                        double total_cost = pcost+ccost;
                        if(total_cost<maxcost) {
                            grouper->setClass(i,j,total_cost);
                            count++;
                        }
                    }
#else
                    debugf("dcost","output %d\n",p.keys.length());
                    for(int index=0;index<p.keys.length();index++) {
                        int j = p.keys[index];
                        if(j<minclass) continue;
                        if(j==reject_class) continue;
                        float value = p.values[index];
                        if(value<=0.0) continue;
                        if(value<minprob) continue;
                        float pcost = -log(value);
                        debugf("dcost","%3d %10g %c\n",j,pcost+ccost,(j>32?j:'_'));
                        double total_cost = pcost+ccost;
                        if(total_cost<maxcost) {
                            if(use_priors) {
                                total_cost -= -log(priors(j));
                            }
                            grouper->setClass(i,j,total_cost);
                            count++;
                        }
                    }
                    debugf("dcost","\n");
#endif
                    if(count==0) {
                        float xheight = 10.0;
                        if(b.height()<xheight/2 && b.width()<xheight/2) {
                            grouper->setClass(i,'~',high_cost/2);
                        } else {
                            grouper->setClass(i,'#',(b.width()/xheight)*high_cost);
                        }
                    }
                    if(grouper->pixelSpace(i)>space_threshold) {
                        debugf("spaces","space %d\n",grouper->pixelSpace(i));
                        grouper->setSpaceCost(i,space_yes,space_no);
                    }
                    // dwait();
                }
            }
            grouper->getLattice(result);
        }

        void align(ustrg &chars,intarray &seg,floatarray &costs,
                   bytearray &image,IGenericFst &transcription) {
            throw Unimplemented();
#if 0
            autodel<FstBuilder> lattice;
            recognizeLine(*lattice,image);
            intarray ids;
            bestpath2(chars,costs,ids,lattice,transcript,false);
            if(debug("info")) {
                for(int i=0;i<chars.length();i++) {
                    debugf("info","%3d %10g %6d %c\n",
                           i,costs(i),ids(i),chars(i));
                }
            }
            intarray aligned_seg;
#endif
        }


    };

    IRecognizeLine *make_Linerec() {
        return new LinerecExtracted();
    }

    void init_linerec() {
        static bool init = false;
        if(init) return;
        init = true;
        component_register<LinerecExtracted>("linerec");
        component_register<CenterFeatureMap>("cfmap");
    }
}
