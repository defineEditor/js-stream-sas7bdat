import DatasetSas7BDat from '../src/index';
import path from 'path';

test('Get unique values of variables', async () => {
    const filePath = path.join(__dirname, 'data', 'sample.sas7bdat');
    const data = new DatasetSas7BDat(filePath);
    const values = await data.getUniqueValues({
        columns: ['NAME', 'AGE', 'WEIGHT'],
        limit: 20,
    });
    expect(values).toMatchSnapshot();
});

test('Get unique values of variables and do not sort the result', async () => {
    const filePath = path.join(__dirname, 'data', 'sample.sas7bdat');
    const data = new DatasetSas7BDat(filePath);
    const values = await data.getUniqueValues({
        columns: ['NAME', 'HEIGHT', 'WEIGHT'],
        limit: 20,
        bufferLength: 10,
        sort: false,
    });
    expect(values).toMatchSnapshot();
});

test('Get unique values with counts', async () => {
    const filePath = path.join(__dirname, 'data', 'sample.sas7bdat');
    const data = new DatasetSas7BDat(filePath);
    const values = await data.getUniqueValues({
        columns: ['NAME', 'AGE', 'WEIGHT'],
        limit: 10,
        addCount: true,
        sort: false,
    });
    expect(values).toMatchSnapshot();
});

test('Get projected object rows without filter', async () => {
    const filePath = path.join(__dirname, 'data', 'sample.sas7bdat');
    const data = new DatasetSas7BDat(filePath);
    const rows = await data.getData({
        start: 0,
        length: 3,
        type: 'object',
        filterColumns: ['WEIGHT', 'NAME'],
    });

    expect(rows).toEqual({
        data: [
            { Weight: 112.5, Name: 'Alfred' },
            { Weight: 84, Name: 'Alice' },
            { Weight: 98, Name: 'Barbara' },
        ],
        lastRow: 2,
        endReached: false,
    });
});

test('Get projected array rows without filter preserves requested order', async () => {
    const filePath = path.join(__dirname, 'data', 'sample.sas7bdat');
    const data = new DatasetSas7BDat(filePath);
    const rows = await data.getData({
        start: 1,
        length: 2,
        type: 'array',
        filterColumns: ['WEIGHT', 'NAME', 'SEX'],
    });

    expect(rows).toEqual({
        data: [
            [84, 'Alice', 'F'],
            [98, 'Barbara', 'F'],
        ],
        lastRow: 2,
        endReached: false,
    });
});

test('Read projected object records across multiple buffers', async () => {
    const filePath = path.join(__dirname, 'data', 'sample.sas7bdat');
    const data = new DatasetSas7BDat(filePath);
    const rows = [];

    for await (const row of data.readRecords({
        start: 1,
        bufferLength: 2,
        type: 'object',
        filterColumns: ['WEIGHT', 'NAME'],
    })) {
        rows.push(row);
        if (rows.length === 3) {
            break;
        }
    }

    expect(rows).toEqual([
        { Weight: 84, Name: 'Alice' },
        { Weight: 98, Name: 'Barbara' },
        { Weight: 102.5, Name: 'Carol' },
    ]);
});
