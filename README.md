# h9-external
Max/MSP External utilizing [libh9](https://github.com/malacalypse/libh9).

## Building

```bash
mkdir build && cd build
cmake ..
make h9-external
```

If you don't like the name, clone it into another directory (it picks up the external name from the parent directory). So if you want your external to be just `h9` put it in that directory. No other changes should be necessary.

## License

The full text of the license should be found in LICENSE.txt, included as part of this repository.

This library and all accompanying code, examples, information and documentation is
Copyright (C) 2019-2020 Daniel Collins

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <https://www.gnu.org/licenses/>.
