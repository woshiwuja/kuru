#include "entt/entity/fwd.hpp"
#include <Kuru.h>
using namespace KR;

struct SaveFile{
    std::istream &is;
    template<typename t>
    void operator()(t &value) {
        static_assert(std::is_trivially_copyable_v<t>);
        is.read(reinterpret_cast<char *>(&value), sizeof(t));
    }

    template<typename t>
    void operator()(entt::entity &e, t &c) { (*this)(e); (*this)(c); }
};

struct saveplugin : public Plugin {
    SaveFile savefile;
    void load(entt::snapshot s);

    template<typename t>
    void save(entt::registry &r){
        for(auto [e, component]:r.view<t>().each()){
            entt::snapshot{r}.get<entt::entity>(savefile).get<t>(savefile);
        }
    }
};
