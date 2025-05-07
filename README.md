# js-stream-sas7bdat
*js-stream-sas7bdat* is a TypeScript library for streaming and processing SAS7BDAT files in Node.js environment. It provides functionalities to read data and metadata from SAS7BDAT files using [ReadStat](https://github.com/WizardMac/ReadStat) library.
The output format matches CDISC Dataset-JSON 1.1 format.

## Features
* Stream SAS7BDAT files
* Extract metadata from SAS7BDAT files
* Read observations as an iterable
* Get unique values from observations
* Filter data

## Installation
Install the library using npm:

```sh
npm install js-stream-sas7bdat
```

## Building Node extension

To build the node native extension:
```sh
npm run build
```

To build on Windows, it is required to compile libiconv outside of this project and place iconv.h to src/binding/libiconv/include and 32-bit and 64-bit versions of libiconv.lib in folders /src/binding/libiconv/x32/ and /src/binding/libiconv/x64/.

The project includes prebuild binaries which were tested for Linux x64 and Windows x64.

## Usage
```TypeScript
dataset = new DatasetSas7BDat(filePath, [options])
```
### Creating Dataset-SAS7BDAT instance
```TypeScript
import DatasetSas7BDat from 'js-stream-sas7bdat';

dataset = new DatasetSas7BDat('/path/to/dataset.sas7bdat')
```

#### Example
```TypeScript
const dataset = new DatasetSas7BDat('/path/to/dataset.sas7bdat');
```

### Getting Metadata
```TypeScript
const metadata = await dataset.getMetadata();
```
### Reading Observations
```TypeScript
// Read first 500 records of a dataset
const data = await dataset.getData({start: 0, length: 500})
```

### Reading Observations as iterable
```TypeScript
// Read dataset starting from position 10 (11th record in the dataset)
for await (const record of dataset.readRecords({start: 10, filterColumns: ["studyId", "uSubjId"], type: "object"})) {
    console.log(record);
}
```

### Getting Unique Values
```TypeScript
const uniqueValues = await dataset.getUniqueValues({ columns: ["studyId", "uSubjId"], limit: 100 });
```

### Applying Filters
You can apply filters to the data when reading observations using the `js-array-filter` package.

#### Example
```TypeScript
import Filter from 'js-array-filter';

// Define a filter
const filter = new Filter('dataset-json1.1', metadata.columns, {
    conditions: [
        { variable: 'AGE', operator: 'gt', value: 55 },
        { variable: 'DCDECOD', operator: 'eq', value: 'STUDY TERMINATED BY SPONSOR' }
    ],
    connectors: ['or']
});

// Apply the filter when reading data
const filteredData = dataset.getData({
    start: 0,
    filter: filter,
    filterColumns: ['USUBJID', 'DCDECOD', 'AGE']
});
console.log(filteredData);
```

## Methods

### `getMetadata`

Returns the metadata of the SAS7BDAT file.

#### Returns

- `Promise<Metadata>`: A promise that resolves to the metadata of the dataset.

#### Example

```typescript
const metadata = await dataset.getMetadata();
console.log(metadata);
```

### `getData`

Reads observations from the dataset.

#### Parameters

- `props` (object): An object containing the following properties:
  - `start` (number, optional): The starting position for reading data.
  - `length` (number, optional): The number of records to read. Defaults to reading all records.
  - `type` (DataType, optional): The type of the returned object ("array" or "object"). Defaults to "array".
  - `filterColumns` (string[], optional): The list of columns to return when type is "object". If empty, all columns are returned.
  - `filter` (Filter, optional): A Filter instance from js-array-filter package used to filter data records.

#### Returns

- `Promise<(ItemDataArray | ItemDataObject)[]>`: A promise that resolves to an array of data records.

#### Example

```typescript
const data = await dataset.getData({ start: 0, length: 500, type: "object", filterColumns: ["studyId", "uSubjId"] });
console.log(data);
```

### `readRecords`

Reads observations as an iterable.

#### Parameters

- `props` (object, optional): An object containing the following properties:
  - `start` (number, optional): The starting position for reading data. Defaults to 0.
  - `type` (DataType, optional): The type of data to return ("array" or "object"). Defaults to "array".
  - `filterColumns` (string[], optional): An array of column names to include in the returned data.

#### Returns

- `AsyncGenerator<ItemDataArray | ItemDataObject, void, undefined>`: An async generator that yields data records.

#### Example

```typescript
for await (const record of dataset.readRecords({ start: 10, filterColumns: ["studyId", "uSubjId"], type: "object" })) {
    console.log(record);
}
```

### `getUniqueValues`

Gets unique values for variables.

#### Parameters

- `props` (object): An object containing the following properties:
  - `columns` (string[]): An array of column names to get unique values for.
  - `limit` (number, optional): The maximum number of unique values to return for each column. Defaults to 100.
  - `bufferLength` (number, optional): The buffer length for reading data. Defaults to 1000.
  - `sort` (boolean, optional): Whether to sort the unique values. Defaults to true.

#### Returns

- `Promise<UniqueValues>`: A promise that resolves to an object containing unique values for the specified columns.

#### Example

```typescript
const uniqueValues = await dataset.getUniqueValues({
    columns: ["studyId", "uSubjId"],
    limit: 100,
    bufferLength: 1000,
    sort: true
});
console.log(uniqueValues);
```
----

## Running Tests
Run the tests using Jest:
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