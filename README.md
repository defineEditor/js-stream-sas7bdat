# js-stream-sas7bdat

js-stream-sas7bdat is a TypeScript library for streaming and processing datasets through the ReadStat C library in Node.js.

It supports these formats:

- SAS: `.sas7bdat`
- Stata: `.dta`
- SPSS: `.sav`, `.zsav`, `.por`

The metadata shape follows CDISC Dataset-JSON 1.1 conventions.

## Features

- Read metadata from SAS, Stata, and SPSS datasets
- Read rows as arrays or keyed objects
- Stream records with async iteration
- Project selected columns
- Filter rows with `js-array-filter`
- Compute unique values per column
- Auto-detect reader type from the file extension

## Installation

```sh
npm install js-stream-sas7bdat
```

## Building Node extension

To build the node native extension:
```sh
npm run build
```

On Windows, `libiconv` must be provided separately. Place `iconv.h` in `src/binding/libiconv/include` and the matching `libiconv.lib` files in the platform folders under `src/binding/libiconv`.

## Usage

### Generic Reader

Use `createDatasetReader` when you want the library to select the correct reader from the file extension.

```ts
import { createDatasetReader } from 'js-stream-sas7bdat';

const dataset = createDatasetReader('/path/to/sample.sav');
const metadata = await dataset.getMetadata();
const rows = await dataset.getData({ start: 0, length: 10 });
```

### Format-Specific Readers

Use the explicit classes when the dataset type is known.

```ts
import DatasetSas7BDat, {
  DatasetReadStat,
  DatasetSpss,
  DatasetStata,
} from 'js-stream-sas7bdat';

const sasDataset = new DatasetSas7BDat('/path/to/sample.sas7bdat');
const stataDataset = new DatasetStata('/path/to/sample.dta');
const spssDataset = new DatasetSpss('/path/to/sample.zsav');

const genericDataset = new DatasetReadStat('/path/to/sample.dat', {
  format: 'dta',
});
```

The default export remains `DatasetSas7BDat` for backward compatibility.

### Reading Metadata

```ts
const metadata = await dataset.getMetadata();
```

### Reading Rows

```ts
const result = await dataset.getData({
  start: 0,
  length: 500,
  type: 'object',
  filterColumns: ['studyId', 'uSubjId'],
});

console.log(result.data);
console.log(result.lastRow);
console.log(result.endReached);
```

`getData()` returns:

```ts
{
  data: (ItemDataArray | ItemDataObject)[];
  lastRow: number;
  endReached: boolean;
}
```

### Streaming Rows

```ts
for await (const record of dataset.readRecords({
  start: 10,
  bufferLength: 1000,
  type: 'object',
  filterColumns: ['studyId', 'uSubjId'],
})) {
  console.log(record);
}
```

### Filtering Rows

Filtering is supported by `getData()` through the `js-array-filter` package.

```ts
import Filter from 'js-array-filter';
import { createDatasetReader } from 'js-stream-sas7bdat';

const dataset = createDatasetReader('/path/to/sample.sas7bdat');
const metadata = await dataset.getMetadata();

const filter = new Filter('dataset-json1.1', metadata.columns, {
  conditions: [
    { variable: 'AGE', operator: 'gt', value: 55 },
    { variable: 'DCDECOD', operator: 'eq', value: 'STUDY TERMINATED BY SPONSOR' },
  ],
  connectors: ['or'],
});

const filtered = await dataset.getData({
  start: 0,
  length: 100,
  type: 'object',
  filter,
  filterColumns: ['USUBJID', 'DCDECOD', 'AGE'],
});

console.log(filtered.data);
```

A `BasicFilter` object can also be passed as `filter`.

### Getting Unique Values

```ts
const uniqueValues = await dataset.getUniqueValues({
  columns: ['studyId', 'uSubjId'],
  limit: 100,
  bufferLength: 1000,
  sort: true,
  addCount: true,
});

console.log(uniqueValues);
```

## API Summary

### `createDatasetReader(filePath, options?)`

Creates a dataset reader by detecting the file format from the extension.

### `new DatasetReadStat(filePath, options?)`

Creates the generic ReadStat-backed reader.

Options:

- `encoding?: BufferEncoding` default `utf8`
- `checkExists?: boolean` default `false`
- `format?: 'sas7bdat' | 'dta' | 'sav' | 'zsav' | 'por'`

### `getMetadata(forceReload?)`

Returns `Promise<DatasetMetadata>`.

### `getData(props)`

Parameters:

- `start?: number`
- `length?: number` use `-1` to read to the end
- `type?: 'array' | 'object'`
- `filterColumns?: string[]`
- `filter?: Filter | BasicFilter`
- `chunkSize?: number`

Returns a promise resolving to an object with `data`, `lastRow`, and `endReached`.

### `readRecords(props?)`

Parameters:

- `start?: number`
- `bufferLength?: number`
- `type?: 'array' | 'object'`
- `filterColumns?: string[]`

Returns `AsyncGenerator<ItemDataArray | ItemDataObject, void, undefined>`.

### `getUniqueValues(props)`

Parameters:

- `columns: string[]`
- `limit?: number`
- `bufferLength?: number`
- `sort?: boolean`
- `addCount?: boolean`

Returns `Promise<UniqueValues>`.

## Running Tests

```sh
npm test
```

## License
This project is licensed under the MIT License. See the LICENSE file for details.

## Author
Dmitry Kolosov

## Contributing
Contributions are welcome! Please open an issue or submit a pull request for any improvements or bug fixes.

For more details, refer to the source code and the documentation.