#include "ecs/sn_object.h"
#include "utils/global.h"

SnObject::SnObject() {
    _sn = Global::GetInstance()->GenerateSN();
}

uint64 SnObject::GetSN() const {
    return _sn;
}

void SnObject::SetSN(uint64 sn)
{
    _sn = sn;
}
