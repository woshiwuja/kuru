
using namespace KR;
struct Cuboid{
        float lenght;
        float width;
        float height;
        float tsa(){
            return 2*(lenght*width + width*height + lenght*height);
        }
        float lsa(){
            return 2*height*(lenght+width);
        }
        float volume(){
            return lenght * width * height;
        }
};

struct Sphere {
    float radius;
};

struct Cylinder{
    float halfheight;
    float radius;
    float volume(){
        return 3.14*(2*halfheight)*(radius*radius);
    }
};
