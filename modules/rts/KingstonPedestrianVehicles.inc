    // Kingston pedestrian vehicle bodies: parked/possessed cars are not Mobile agents.
    struct VehicleBody { Entity e; Vec2 pos,axis,velocity; Fixed length,width; };
    std::vector<VehicleBody> vehicleBodies;
    if(driving_&&driving_->active()) w.each<Unit,Transform>([&](Entity e,const Unit&u,const Transform&t){
        if(u.hp.raw<=0||(u.flags&kUnitOffMap))return;
        for(const auto&v:driving_->rules().vehicles)if(v.type==u.type){
            Vec2 axis=normalize(u.facing.xz());if(!axis.lengthSq().raw)axis={kOne,Fixed{}};
            Vec2 velocity{};if(auto*c=driving_->car(sim,e))velocity=c->st.velocity.xz()/driving_->rules().metersPerCell;
            vehicleBodies.push_back({e,t.pos.xz(),axis,velocity,v.length/2,v.width/2});break;
        }
    });
    auto vehicleSegment=[&](const VehicleBody&v,Vec2 a,Vec2 b,Fixed radius){
        Vec2 side{-v.axis.y,v.axis.x},start=a-v.pos,delta=b-a;
        Fixed lo{},hi=kOne;
        auto slab=[&](Fixed at,Fixed step,Fixed bound){
            if(!step.raw)return fxAbs(at)<bound;
            Fixed first=(-bound-at)/step,last=(bound-at)/step;if(first>last)std::swap(first,last);
            lo=fxMax(lo,first);hi=fxMin(hi,last);return lo<hi;
        };
        return slab(start.dot(v.axis),delta.dot(v.axis),v.length+radius)&&slab(start.dot(side),delta.dot(side),v.width+radius)&&hi>Fixed{}&&lo<kOne;
    };
    auto vehicleDetour=[&](Entity self,Vec2 start,Vec2 goal,Fixed radius){
        const Fixed pad=radius+Fixed::fromRatio(8,100);
        std::vector<const VehicleBody*> near;
        for(const auto&v:vehicleBodies)if(v.e!=self&&(v.pos-start).lengthSq()<Fixed::fromInt(25))near.push_back(&v);
        bool blocked=false;for(auto*v:near)blocked=blocked||vehicleSegment(*v,start,goal,pad);
        if(!blocked)return goal;
        std::stable_sort(near.begin(),near.end(),[&](auto*a,auto*b){return (a->pos-start).lengthSq()<(b->pos-start).lengthSq();});
        if(near.size()>12)near.resize(12);
        std::vector<Vec2> points{start,goal};
        for(auto*v:near){Vec2 side{-v->axis.y,v->axis.x};Fixed l=v->length+pad+Fixed::fromRatio(12,100),b=v->width+pad+Fixed::fromRatio(12,100);
            for(int i:{-1,1})for(int j:{-1,1}){auto corner=v->pos+v->axis*(l*i)+side*(b*j);bool clear=true;
                for(auto*o:near)if(vehicleSegment(*o,corner,corner,pad)){clear=false;break;}
                if(clear)points.push_back(corner);
            }
        }
        auto visible=[&](size_t i,size_t j){for(auto*v:near){
            if((i==0||j==0)&&vehicleSegment(*v,start,start,pad))continue;
            if(vehicleSegment(*v,points[i],points[j],pad))return false;
        }return true;};
        const Fixed inf=Fixed::fromInt(1000000);std::vector<Fixed> cost(points.size(),inf);std::vector<int> next(points.size(),-1);std::vector<bool> done(points.size(),false);cost[1]={};
        // A small visibility graph handles rows of parked cars as a single detour.
        for(size_t n=0;n<points.size();++n){int best=-1;for(size_t i=0;i<points.size();++i)if(!done[i]&&cost[i]<inf&&(best<0||cost[i]<cost[best]))best=int(i);
            if(best<0||best==0)break;done[best]=true;
            for(size_t i=0;i<points.size();++i)if(!done[i]&&visible(i,size_t(best))){auto c=cost[best]+(points[i]-points[best]).length();if(c<cost[i]){cost[i]=c;next[i]=best;}}
        }
        return next[0]>=0?points[size_t(next[0])]:start;
    };

    auto vehicleCrossingWait=[&](Entity person,Vec2 pos,Vec2 preferred,Fixed radius){
        const auto key="resident_crossing_"+std::to_string(person.packed())+"_";
        gs.trig.vars[key+"reason"]={};
        if(!preferred.lengthSq().raw)return false;
        bool danger=false,occluded=false;
        for(const auto&v:vehicleBodies){if(v.e==person)continue;
            const auto relative=pos-v.pos;const auto reach=v.velocity.length()*Fixed::fromInt(3)+v.length+Fixed::fromInt(3);
            if(relative.lengthSq()>reach*reach)continue;
            if(v.velocity.length()>Fixed::fromRatio(8,100)){
                if(vehicleSegment(v,pos,pos,radius))continue; // let someone already exposed escape
                const auto horizon=fxClamp((v.width*2+radius*2)/fxMax(preferred.length(),Fixed::fromRatio(1,5))+Fixed::fromRatio(1,2),Fixed::fromInt(1),Fixed::fromInt(3));
                if(kingston::crossingDanger(relative,preferred-v.velocity,v.axis,v.length,v.width,radius+Fixed::fromRatio(1,5),horizon))danger=true;
            }else if(v.length>Fixed::fromRatio(3,2)){
                const Vec2 side{-v.axis.y,v.axis.x};
                occluded=occluded||(fxAbs(relative.dot(v.axis))<v.length+Fixed::fromRatio(3,5)&&fxAbs(relative.dot(side))<v.width+Fixed::fromRatio(4,5)&&fxAbs(preferred.dot(side))>preferred.length()/2);
            }
        }
        if(danger){gs.trig.vars[key+"reason"]=Fixed::fromInt(1);gs.trig.vars[key+"clear_until"]=Fixed::fromInt(tick+sim.tickRate()/2);return true;}
        if(gs.trig.vars[key+"clear_until"]>Fixed::fromInt(tick)){gs.trig.vars[key+"reason"]=Fixed::fromInt(1);return true;}
        if(occluded&&!gs.trig.vars[key+"peeked"].raw){gs.trig.vars[key+"peeked"]=Fixed::fromInt(1);gs.trig.vars[key+"peek_until"]=Fixed::fromInt(tick+sim.tickRate());}
        if(!occluded)gs.trig.vars[key+"peeked"]={};
        if(gs.trig.vars[key+"peek_until"]>Fixed::fromInt(tick)){gs.trig.vars[key+"reason"]=Fixed::fromInt(2);return true;}
        return false;
    };
