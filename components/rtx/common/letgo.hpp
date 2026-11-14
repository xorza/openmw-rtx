#pragma once

namespace Rtx
{
    /// Empties `held` and gives its storage back. **Not `held = {}`**, which a `std::vector` takes as the assignment
    /// of an empty initializer list, keeping every byte it held.
    template <class Container>
    void letGoOf(Container& held)
    {
        held = Container();
    }
}
