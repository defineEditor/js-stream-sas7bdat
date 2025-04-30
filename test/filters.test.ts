import DatasetSas7BDat from '../src/index';
import Filter from 'js-array-filter';
import path from 'path';

test('Get filtered rows of dataset with simple "and" filter', async () => {
    const filePath = path.join(__dirname, '/data/sample.sas7bdat');

    const data = new DatasetSas7BDat(filePath);

    const metadata = await data.getMetadata();

    const filter = new Filter('dataset-json1.1', metadata.columns, {
        conditions: [
            { variable: 'AGE', operator: 'gt', value: 13 },
            { variable: 'SEX', operator: 'eq', value: 'M' }
        ],
        connectors: ['and']
    });
    const rows = await data.getData({ start: 0, length: 5, filter, filterColumns: ['NAME', 'SEX', 'AGE'] });
    expect(rows.length).toBeLessThanOrEqual(5);
    expect(rows).toMatchSnapshot();
});

test('Get filtered rows of dataset with simple "or" filter', async () => {
    const filePath = path.join(__dirname, '/data/sample.sas7bdat');

    const data = new DatasetSas7BDat(filePath);
    const metadata = await data.getMetadata();

    const filter = new Filter('dataset-json1.1', metadata.columns, {
        conditions: [
            { variable: 'AGE', operator: 'gt', value: 12 },
            { variable: 'SEX', operator: 'eq', value: 'M' }
        ],
        connectors: ['or']
    });
    const rows = await data.getData({ start: 0, filter, filterColumns: ['NAME', 'SEX', 'AGE'] });
    expect(rows.length).toEqual(16);
});